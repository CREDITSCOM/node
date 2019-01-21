/**
 *  @file fee.cpp
 *  @author Sergey Sychev
 */

#include "csnode/fee.hpp"

#include <cinttypes>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>

#include <csdb/amount_commission.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <csnode/blockchain.hpp>
#include <transactionspacket.hpp>

namespace cs {
namespace {
constexpr auto kMaxRoundNumWithFixedFee = 10;
constexpr auto kLengthOfCommonTransaction = 152;
constexpr double kMarketRateCS = 0.18;
constexpr double kFixedOneByteFee = 0.001 / kMarketRateCS / kLengthOfCommonTransaction;
constexpr double kNodeRentalCostPerDay = 100. / 30.5 / kMarketRateCS;
constexpr size_t kNumOfBlocksToCountFrequency = 100;
constexpr double kMinFee = 0.0001428;
constexpr size_t kBlocksNumForNodesQtyEstimation = 100;
}  // namespace

Fee::Fee()
    : num_of_last_block_(0),
    total_transactions_length_(0),
    one_byte_cost_(0),
    one_round_cost_(0),
    rounds_frequency_(0),
    current_pool_(nullptr),
    transactions_packet_(nullptr) {}

void Fee::CountFeesInPool(const BlockChain& blockchain, csdb::Pool* pool) {
  if (pool->transactions().size() < 1) {
    return;
  }
  if (num_of_last_block_ > blockchain.getLastSequence() + 1) {
    ResetTrustedCache(blockchain);
  }
  Init(blockchain, pool);
  CountOneByteCost(blockchain);
  SetCountedFee();
}

void Fee::CountFeesInPool(const BlockChain& blockchain, TransactionsPacket* packet) {
  if (packet->transactionsCount() < 1) {
    return;
  }
  Init(blockchain, packet);
  CountOneByteCost(blockchain);
  SetCountedFee();
}

inline void Fee::Init(const BlockChain& blockchain, csdb::Pool* pool) {
  current_pool_ = pool;
  transactions_packet_ = nullptr;
  num_of_last_block_ = blockchain.getLastSequence() + 1;
}

inline void Fee::Init(const BlockChain& blockchain, TransactionsPacket* packet) {
  transactions_packet_ = packet;
  current_pool_ = nullptr;
  num_of_last_block_ = blockchain.getLastSequence() + 1;
}

void Fee::SetCountedFee() {
  if (current_pool_ != nullptr) {
    std::vector<csdb::Transaction>& transactions = current_pool_->transactions();
    size_t size_of_transaction;
    double counted_fee;
    for (auto& transaction : transactions) {
      size_of_transaction = transaction.to_byte_stream().size();
      counted_fee = one_byte_cost_ * size_of_transaction;
      if (counted_fee < kMinFee) {
        transaction.set_counted_fee(csdb::AmountCommission(kMinFee));
      }
      else {
        transaction.set_counted_fee(csdb::AmountCommission(counted_fee));
      }
    }
  }
  else {
    std::vector<csdb::Transaction>& transactions = transactions_packet_->transactions();
    size_t size_of_transaction;
    double counted_fee;
    for (size_t i = 0; i < transactions.size(); ++i) {
      size_of_transaction = transactions[i].to_byte_stream().size();
      counted_fee = one_byte_cost_ * size_of_transaction;
      if (counted_fee < kMinFee) {
        transactions[i].set_counted_fee(csdb::AmountCommission(kMinFee));
      }
      else {
        transactions[i].set_counted_fee(csdb::AmountCommission(counted_fee));
      }
    }
  }
}

void Fee::CountOneByteCost(const BlockChain& blockchain) {
  if (num_of_last_block_ == 0) {
    one_byte_cost_ = 0;
    return;
  }

  if (num_of_last_block_ <= kMaxRoundNumWithFixedFee) {
    one_byte_cost_ = kFixedOneByteFee;
    return;
  }

  CountTotalTransactionsLength();
  CountOneRoundCost(blockchain);
  one_byte_cost_ = one_round_cost_ / total_transactions_length_;
}

void Fee::CountTotalTransactionsLength() {
  total_transactions_length_ = 0;
  std::vector<csdb::Transaction> transactions;
  if (current_pool_ != nullptr) {
    transactions = current_pool_->transactions();
  }
  else {
    transactions = transactions_packet_->transactions();
  }
  for (auto & transaction : transactions) {
    total_transactions_length_ += transaction.to_byte_stream().size();
  }
}

void Fee::CountOneRoundCost(const BlockChain& blockchain) {
  CountRoundsFrequency(blockchain);
  double num_of_rounds_per_day = 60 * 60 * 24 / rounds_frequency_;
  if (num_of_rounds_per_day < 1) {
    num_of_rounds_per_day = 1;
  }
  one_round_cost_ = (kNodeRentalCostPerDay * EstimateNumOfNodesInNetwork(blockchain)) / num_of_rounds_per_day;
}

size_t Fee::EstimateNumOfNodesInNetwork(const BlockChain& blockchain) {
  size_t last_sequence = blockchain.getLastSequence();
  size_t sequence_gap = 5; // case of removing last block
  csdb::Pool pool = blockchain.loadBlock(last_sequence - sequence_gap);
  const auto& confidants = pool.confidants();
  
  for (size_t i = 0; i < confidants.size(); ++i) {
    last_trusted_.insert(confidants[i]);
  }

  if (last_sequence > kBlocksNumForNodesQtyEstimation) {
    csdb::Pool pool_to_remove_conf = blockchain.loadBlock(last_sequence - kBlocksNumForNodesQtyEstimation);
    const auto& confidants_to_remove = pool_to_remove_conf.confidants();
    for (size_t i = 0; i < confidants_to_remove.size(); ++i) {
      auto it = last_trusted_.find(confidants_to_remove[i]);
      if (it != last_trusted_.end()) {
        last_trusted_.erase(it);
      }
    }
  }

  std::set<cs::PublicKey> unique_trusted;
  for (const auto& it : last_trusted_) {
    unique_trusted.insert(it);
  }
  return unique_trusted.size();
}

void Fee::CountRoundsFrequency(const BlockChain& blockchain) {
  if (num_of_last_block_ <= kMaxRoundNumWithFixedFee) {
    return;
  }

  size_t block_number_from;
  if (num_of_last_block_ > kNumOfBlocksToCountFrequency) {
    block_number_from = num_of_last_block_ - kNumOfBlocksToCountFrequency;
  }
  else {
    block_number_from = 1;
  }

  double time_stamp_diff = CountBlockTimeStampDifference(block_number_from, blockchain);
  if(fabs(time_stamp_diff) > std::numeric_limits<double>::epsilon()) {
    rounds_frequency_ = time_stamp_diff / (num_of_last_block_ - block_number_from + 1) / 1000;
  }
}

double Fee::CountBlockTimeStampDifference(size_t num_block_from, const BlockChain& blockchain) {
  csdb::PoolHash block_from_hash = blockchain.getHashBySequence(num_block_from);
  csdb::Pool block_from = blockchain.loadBlock(block_from_hash);
  if (!block_from.is_valid()) {
    cserror() << "Fee> " << blockchain.getStorage().last_error_message();
  }
  double time_stamp_from = std::stod(block_from.user_field(0).value<std::string>());

  csdb::PoolHash block_to_hash = blockchain.getLastHash();
  csdb::Pool block_to = blockchain.loadBlock(block_to_hash);
  double time_stamp_to = time_stamp_from;
  const auto str = block_to.user_field(0).value<std::string>();
  if(!str.empty()) {
    try {
      time_stamp_to = std::stod(str);
    }
    catch(...) {
    }
  }

  return time_stamp_to - time_stamp_from;
}

void Fee::ResetTrustedCache(const BlockChain& blockchain) {
  last_trusted_.clear();
  size_t last_sequence = blockchain.getLastSequence();
  if (last_sequence == 0) {
    return;
  }
  csdb::Pool pool = blockchain.loadBlock(last_sequence);
  if (last_sequence <= kBlocksNumForNodesQtyEstimation) {
    while (pool.is_valid()) {
      const auto& confidants = pool.confidants();
      for (size_t i = 0; i < confidants.size(); ++i) {
        last_trusted_.insert(confidants[i]);
      }
      --last_sequence;
      pool = blockchain.loadBlock(last_sequence);
    }
  } else {
    size_t sequence_to_stop = last_sequence - kBlocksNumForNodesQtyEstimation;
    while (last_sequence > sequence_to_stop && pool.is_valid()) {
      const auto& confidants = pool.confidants();
      for (size_t i = 0; i < confidants.size(); ++i) {
        last_trusted_.insert(confidants[i]);
      }
      --last_sequence;
      pool = blockchain.loadBlock(last_sequence);
    }
  }
}
}  // namespace cs
