/**
*  @file fee.cpp
*  @author Sergey Sychev
*/

#include "solver/Fee.h"

#include <cstdint>
#include <vector>
#include <string>

#include <csdb/transaction.h>
#include <csdb/pool.h>
#include <csdb/amount_commission.h>
#include <csnode/blockchain.hpp>
#include <csnode/conveyer.hpp>
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
}  // namespace

Fee::Fee()
    : num_of_nodes_(0),
    num_of_last_block_(0),
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
  num_of_last_block_ = blockchain.getLastWrittenSequence() + 1;
  // Now we don't have tools to estimate number of all nodes in the network.
  // So we use number of trusted. In fact it is a constant. Will be fixed soon.
  num_of_nodes_ = cs::Conveyer::instance().roundTable().confidants.size();
}

inline void Fee::Init(const BlockChain& blockchain, TransactionsPacket* packet) {
  transactions_packet_ = packet;
  current_pool_ = nullptr;
  num_of_last_block_ = blockchain.getLastWrittenSequence() + 1;
  // Now we don't have tools to estimate number of all nodes in the network.
  // So we use number of trusted. In fact it is a constant. Will be fixed soon.
  num_of_nodes_ = cs::Conveyer::instance().roundTable().confidants.size();
}

void Fee::SetCountedFee() {
  if (current_pool_ != nullptr) {
    std::vector<csdb::Transaction>& transactions = current_pool_->transactions();
    size_t size_of_transaction;
    double counted_fee;
    for (size_t i = 0; i < transactions.size(); ++i) {
      size_of_transaction = transactions[i].to_byte_stream().size();
      counted_fee = one_byte_cost_ * size_of_transaction;
      if (counted_fee < kMinFee) {
        transactions[i].set_counted_fee(kMinFee);
      } else {
        transactions[i].set_counted_fee(counted_fee);
      }
    }
  } else {
    std::vector<csdb::Transaction>& transactions = transactions_packet_->transactions();
    size_t size_of_transaction;
    double counted_fee;
    for (size_t i = 0; i < transactions.size(); ++i) {
      size_of_transaction = transactions[i].to_byte_stream().size();
      counted_fee = one_byte_cost_ * size_of_transaction;
      if (counted_fee < kMinFee) {
        transactions[i].set_counted_fee(kMinFee);
      }
      else {
        transactions[i].set_counted_fee(counted_fee);
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
  } else {
    CountTotalTransactionsLength();
    CountOneRoundCost(blockchain);
    one_byte_cost_ = one_round_cost_ / total_transactions_length_;
  }
}

inline void Fee::CountTotalTransactionsLength() {
  total_transactions_length_ = 0;
  std::vector<csdb::Transaction> transactions;
  if (current_pool_ != nullptr) {
    transactions = current_pool_->transactions();
  } else {
    transactions = transactions_packet_->transactions();
  }
  for (size_t i = 0; i < transactions.size(); ++i) {
    total_transactions_length_ += transactions[i].to_byte_stream().size();
  }
}

void Fee::CountOneRoundCost(const BlockChain& blockchain) {
  CountRoundsFrequency(blockchain);
  double num_of_rounds_per_day = 60 * 60 * 24 * rounds_frequency_;
  if (num_of_rounds_per_day < 1) {
    num_of_rounds_per_day = 1;
  }
  one_round_cost_ = (kNodeRentalCostPerDay * num_of_nodes_) / num_of_rounds_per_day; 
}

void Fee::CountRoundsFrequency(const BlockChain& blockchain) {
  if (num_of_last_block_ <= kMaxRoundNumWithFixedFee) {
    return;
  }
  size_t block_number_from;
  if (num_of_last_block_ > kNumOfBlocksToCountFrequency) {
    block_number_from = num_of_last_block_ - kNumOfBlocksToCountFrequency;
  } else {
    block_number_from = 1;
  }
  double time_stamp_diff = CountBlockTimeStampDifference(block_number_from, blockchain);
  rounds_frequency_ = time_stamp_diff / (num_of_last_block_ - block_number_from + 1) / 1000;
}

double Fee::CountBlockTimeStampDifference(size_t num_block_from, const BlockChain& blockchain) {
  csdb::PoolHash block_from_hash = blockchain.getHashBySequence(num_block_from);
  csdb::Pool block_from = blockchain.loadBlock(block_from_hash);
  double time_stamp_from = std::stod(block_from.user_field(0).value<std::string>());

  csdb::PoolHash block_to_hash = blockchain.getLastHash();
  csdb::Pool block_to = blockchain.loadBlock(block_to_hash);
  double time_stamp_to = std::stod(block_to.user_field(0).value<std::string>());

  return time_stamp_to - time_stamp_from;
}
} // namespace Credits
