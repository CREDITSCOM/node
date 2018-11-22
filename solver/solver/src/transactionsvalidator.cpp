#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <csdb/amount_commission.h>
#include <client/params.hpp>
#include <lib/system/logger.hpp>
#include <solver/transactionsvalidator.hpp>

namespace cs {
TransactionsValidator::TransactionsValidator(WalletsState& walletsState, const Config& config)
: config_(config)
, walletsState_(walletsState)
, cntRemovedTrxs_(0) {
  negativeNodes_.reserve(config_.initialNegNodesNum_);
}

void TransactionsValidator::reset(size_t transactionsNum) {
  trxList_.clear();
  trxList_.resize(transactionsNum, WalletsState::noInd_);

  negativeNodes_.clear();
  cntRemovedTrxs_ = 0;
}

bool TransactionsValidator::validateTransaction(const csdb::Transaction& trx, size_t trxInd, uint8_t& del1) {
  if (!validateTransactionAsSource(trx, trxInd, del1))
    return false;
  return validateTransactionAsTarget(trx);
}

bool TransactionsValidator::validateTransactionAsSource(const csdb::Transaction& trx, size_t trxInd, uint8_t& del1) {
  WalletsState::WalletId walletId{};
  WalletsState::WalletData& wallState = walletsState_.getData(trx.source(), walletId);

#ifndef WITHOUT_DELTA
  auto newBalance = wallState.balance_ - trx.amount() - trx.counted_fee();

#ifdef _MSC_VER
  int8_t bitcnt = static_cast<decltype(bitcnt)>(__popcnt(newBalance.integral()) + __popcnt64(newBalance.fraction()));
#else
  int8_t bitcnt = __builtin_popcount(newBalance.integral()) + __builtin_popcountl(newBalance.fraction());
#endif

#ifndef SPAMMER
  if (!wallState.trxTail_.isAllowed(trx.innerID())) {
    del1 = -bitcnt;
    return false;
  }
#endif

  del1 = bitcnt;
  wallState.balance_ = newBalance;
#else
#ifndef SPAMMER
  if (!wallState.trxTail_.isAllowed(trx.innerID()))
    return false;
#endif

  wallState.balance_ = wallState.balance_ - trx.amount() - trx.counted_fee();
#endif
  wallState.trxTail_.push(trx.innerID());

  trxList_[trxInd] = wallState.lastTrxInd_;
  wallState.lastTrxInd_ = static_cast<decltype(wallState.lastTrxInd_)>(trxInd);

  walletsState_.setModified(walletId);

  if (wallState.balance_ < zeroBalance_)
    negativeNodes_.push_back(&wallState);

  return true;
}

bool TransactionsValidator::validateTransactionAsTarget(const csdb::Transaction& trx) {
  WalletsState::WalletId walletId{};
  WalletsState::WalletData& wallState = walletsState_.getData(trx.target(), walletId);

  wallState.balance_ = wallState.balance_ + trx.amount();

  walletsState_.setModified(walletId);
  return true;
}

void TransactionsValidator::validateByGraph(CharacteristicMask& maskIncluded, const Transactions& trxs,
                                            csdb::Pool& trxsExcluded) {
  while (!negativeNodes_.empty()) {
    Node& currNode = *negativeNodes_.back();
    negativeNodes_.pop_back();

    if (currNode.balance_ >= zeroBalance_)
      continue;

    removeTransactions(currNode, trxs, maskIncluded, trxsExcluded);
  }
}

void TransactionsValidator::removeTransactions(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded,
                                               csdb::Pool& trxsExcluded) {
  if (removeTransactions_PositiveOne(node, trxs, maskIncluded, trxsExcluded))
    return;
  if (removeTransactions_PositiveAll(node, trxs, maskIncluded, trxsExcluded))
    return;
  if (removeTransactions_NegativeOne(node, trxs, maskIncluded, trxsExcluded))
    return;
  if (removeTransactions_NegativeAll(node, trxs, maskIncluded, trxsExcluded))
    return;

  csdebug() << "removeTransactions: Failed to make balance non-negative ";
}

bool TransactionsValidator::removeTransactions_PositiveOne(Node& node, const Transactions& trxs,
                                                           CharacteristicMask& maskIncluded, csdb::Pool& trxsExcluded) {
  if (node.balance_ >= zeroBalance_)
    return true;

  const csdb::Amount absBalance = -node.balance_;
  TransactionIndex* prevNext = &node.lastTrxInd_;

  for (TransactionIndex trxInd = *prevNext; trxInd != WalletsState::noInd_; trxInd = *prevNext) {
    const csdb::Transaction& trx = trxs[trxInd];
    const csdb::Amount& trxCost = trx.amount() + trx.counted_fee();

    if (trxCost < absBalance) {
      prevNext = &trxList_[trxInd];
      continue;
    }

    WalletsState::WalletId walletId{};
    Node& destNode = walletsState_.getData(trx.target(), walletId);

    const bool isTrxPositive = (trx.amount() <= destNode.balance_);

    if (!isTrxPositive) {
      prevNext = &trxList_[trxInd];
      continue;
    }

    maskIncluded[trxInd] = 0;

#ifndef WITHOUT_BAD_BLOCK
    trxsExcluded.add_transaction(trx);
#endif

    node.balance_ = node.balance_ + trx.amount() + trx.counted_fee();
    destNode.balance_ = destNode.balance_ - trx.amount();

    *prevNext = trxList_[trxInd];
    trxList_[trxInd] = WalletsState::noInd_;

    ++cntRemovedTrxs_;

    return true;
  }

  return false;
}

bool TransactionsValidator::removeTransactions_PositiveAll(Node& node, const Transactions& trxs,
                                                           CharacteristicMask& maskIncluded, csdb::Pool& trxsExcluded) {
  if (node.balance_ >= zeroBalance_)
    return true;

  TransactionIndex* prevNext = &node.lastTrxInd_;

  for (TransactionIndex trxInd = *prevNext; trxInd != WalletsState::noInd_; trxInd = *prevNext) {
    const csdb::Transaction& trx = trxs[trxInd];

    WalletsState::WalletId walletId{};
    Node& destNode = walletsState_.getData(trx.target(), walletId);
    const bool isTrxPositive = (trx.amount() <= destNode.balance_);

    if (!isTrxPositive) {
      prevNext = &trxList_[trxInd];
      continue;
    }

    maskIncluded[trxInd] = 0;

#ifndef WITHOUT_BAD_BLOCK
    trxsExcluded.add_transaction(trx);
#endif

    node.balance_ = node.balance_ + trx.amount() + trx.counted_fee();
    destNode.balance_ = destNode.balance_ - trx.amount();

    *prevNext = trxList_[trxInd];
    trxList_[trxInd] = WalletsState::noInd_;

    ++cntRemovedTrxs_;

    if (node.balance_ >= zeroBalance_)
      return true;
  }

  return false;
}

bool TransactionsValidator::removeTransactions_NegativeOne(Node& node, const Transactions& trxs,
                                                           CharacteristicMask& maskIncluded, csdb::Pool& trxsExcluded) {
  if (node.balance_ >= zeroBalance_)
    return true;

  const csdb::Amount absBalance = -node.balance_;
  TransactionIndex* prevNext = &node.lastTrxInd_;

  for (TransactionIndex trxInd = *prevNext; trxInd != WalletsState::noInd_; trxInd = *prevNext) {
    const csdb::Transaction& trx = trxs[trxInd];
    const csdb::Amount& trxCost = trx.amount() + trx.counted_fee();

    if (trxCost < absBalance) {
      prevNext = &trxList_[trxInd];
      continue;
    }

    WalletsState::WalletId walletId{};
    Node& destNode = walletsState_.getData(trx.target(), walletId);

    maskIncluded[trxInd] = 0;

#ifndef WITHOUT_BAD_BLOCK
    trxsExcluded.add_transaction(trx);
#endif

    node.balance_ = node.balance_ + trx.amount() + trx.counted_fee();
    destNode.balance_ = destNode.balance_ - trx.amount();

    *prevNext = trxList_[trxInd];
    trxList_[trxInd] = WalletsState::noInd_;

    ++cntRemovedTrxs_;

    if (destNode.balance_ < zeroBalance_)
      negativeNodes_.push_back(&destNode);

    return true;
  }
  return false;
}

bool TransactionsValidator::removeTransactions_NegativeAll(Node& node, const Transactions& trxs,
                                                           CharacteristicMask& maskIncluded, csdb::Pool& trxsExcluded) {
  if (node.balance_ >= zeroBalance_)
    return true;

  TransactionIndex* prevNext = &node.lastTrxInd_;

  for (TransactionIndex trxInd = *prevNext; trxInd != WalletsState::noInd_; trxInd = *prevNext) {
    const csdb::Transaction& trx = trxs[trxInd];

    WalletsState::WalletId walletId{};
    Node& destNode = walletsState_.getData(trx.target(), walletId);

    maskIncluded[trxInd] = 0;

#ifndef WITHOUT_BAD_BLOCK
    trxsExcluded.add_transaction(trx);
#endif

    node.balance_ = node.balance_ + trx.amount() + trx.counted_fee();
    destNode.balance_ = destNode.balance_ - trx.amount();

    *prevNext = trxList_[trxInd];
    trxList_[trxInd] = WalletsState::noInd_;

    ++cntRemovedTrxs_;

    if (destNode.balance_ < zeroBalance_)
      negativeNodes_.push_back(&destNode);

    if (node.balance_ >= zeroBalance_)
      return true;
  }

  return false;
}
}  // namespace cs
