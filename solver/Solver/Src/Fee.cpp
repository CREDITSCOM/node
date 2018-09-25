/**
*  @file fee.cpp
*  @author Sergey Sychev
*/

#include "Solver/Fee.h"

#include <cstdint>
#include <vector>
#include <string>

#include <csdb/transaction.h>
#include <csdb/pool.h>
#include <csdb/amount_commission.h>
#include <csnode/node.hpp>

namespace Credits {
namespace {
constexpr auto kMaxRoundNumWithFixedFee = 10;
constexpr auto kLengthOfCommonTransaction = 152;
constexpr double kMarketRateCS = 0.18;
constexpr double kFixedOneByteFee = 0.001 / kMarketRateCS / kLengthOfCommonTransaction;
constexpr double kNodeRentalCostPerDay = 100. / 30.5 / kMarketRateCS;
constexpr size_t kNumOfBlocksToCountFrequency = 100;
}  // namespace

Fee::Fee()
    : num_of_trusted_nodes_(0),
    num_of_last_block_(0),
    total_transactions_length_(0),
    one_byte_cost_(0),
    one_round_cost_(0),
    round_frequency_(0),
    current_pool_(nullptr),
    node_(nullptr) {}

void Fee::CountFeesInPool(Node* node, csdb::Pool* pool,
    size_t pool_sequence, size_t num_of_trusted_nodes) {
  if (pool->transactions().size() < 1) {
    return;
  }
  Init(node, pool, pool_sequence, num_of_trusted_nodes);
  GetOneByteCost();
  SetCountedFee();
}

inline void Fee::Init(Node* node, csdb::Pool* pool,
    size_t pool_sequence, size_t num_of_trusted_nodes) {
  current_pool_ = pool;
  num_of_last_block_ = pool_sequence;
  num_of_trusted_nodes_ = num_of_trusted_nodes;
  node_ = node;
}

void Fee::SetCountedFee() {
  std::vector<csdb::Transaction>& transactions = current_pool_->transactions();
  size_t size_of_transaction;
  for (size_t i = 0; i < transactions.size(); ++i) {
    size_of_transaction = transactions[i].to_byte_stream().size();
    transactions[i].set_counted_fee(one_byte_cost_ * size_of_transaction);
  }
}

void Fee::GetOneByteCost() {
  if (num_of_last_block_ == 0) {
    one_byte_cost_ = 0;
    return;
  }

  if (num_of_last_block_ <= kMaxRoundNumWithFixedFee) {
    one_byte_cost_ = kFixedOneByteFee;
    return;
  } else {
    CountTotalTransactionsLength();
    GetOneRoundCost();
    one_byte_cost_ = one_round_cost_ / total_transactions_length_;
  }
}

inline void Fee::CountTotalTransactionsLength() {
  total_transactions_length_ = 0;
  auto transactions = current_pool_->transactions();
  for (size_t i = 0; i < transactions.size(); ++i) {
    total_transactions_length_ += transactions[i].to_byte_stream().size();
  }
}

void Fee::GetOneRoundCost() {
  GetRoundFrequency();
  double num_of_rounds_per_day = 60 * 60 * 24 * round_frequency_;
  if (num_of_rounds_per_day < 1) {
    num_of_rounds_per_day = 1;
  }
  one_round_cost_ = (kNodeRentalCostPerDay * num_of_trusted_nodes_) / num_of_rounds_per_day; 
}

void Fee::GetRoundFrequency() {
  if (num_of_last_block_ <= kMaxRoundNumWithFixedFee) {
    return;
  }
  size_t block_number_from;
  if (num_of_last_block_ > kNumOfBlocksToCountFrequency) {
    block_number_from = num_of_last_block_ - kNumOfBlocksToCountFrequency;
  } else {
    block_number_from = 1;
  }
  double time_stamp_diff = GetBlockTimeStampDifference(block_number_from);
  round_frequency_ = time_stamp_diff / (num_of_last_block_ - block_number_from + 1) / 1000;
}

double Fee::GetBlockTimeStampDifference(size_t num_block_from) {
  csdb::PoolHash block_from_hash = node_->getBlockChain().getHashBySequence(num_block_from);
  csdb::Pool block_from = node_->getBlockChain().loadBlock(block_from_hash);
  double time_stamp_from = std::stod(block_from.user_field(0).value<std::string>());

  csdb::PoolHash block_to_hash = node_->getBlockChain().getLastHash();
  csdb::Pool block_to = node_->getBlockChain().loadBlock(block_to_hash);
  double time_stamp_to = std::stod(block_to.user_field(0).value<std::string>());

  return (time_stamp_to - time_stamp_from);
}

}  // namespace Credits