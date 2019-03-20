#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <vector>
#include <map>

#include <csdb/amount.hpp>
#include <csdb/amount_commission.hpp>
#include <client/params.hpp>
#include <lib/system/logger.hpp>
#include <csnode/transactionsvalidator.hpp>
#include <smartcontracts.hpp>
#include <solvercontext.hpp>
#include <walletscache.hpp>

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

static const char * log_prefix = "Validator: ";

bool TransactionsValidator::validateTransaction(SolverContext& context, const Transactions& trxs,
                                                size_t trxInd) {
  if (!validateTransactionAsSource(context, trxs, trxInd)) {
    return false;
  }
  return validateTransactionAsTarget(trxs[trxInd]);
}

bool TransactionsValidator::validateTransactionAsSource(SolverContext& context, const Transactions& trxs,
                                                        size_t trxInd) {
  const auto& trx = trxs[trxInd];
  WalletsState::WalletId walletId{};
  WalletsState::WalletData& wallState = walletsState_.getData(trx.source(), walletId);
  csdb::Amount newBalance;
  auto& smarts = context.smart_contracts();

#ifndef SPAMMER
  if (!wallState.trxTail_.isAllowed(trx.innerID())) {
    csdebug() << log_prefix << "trx rejected, incorrect innerID";
    if (SmartContracts::is_new_state(trx)) {
      rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
    }
    return false;
  }
#endif

  if (SmartContracts::is_new_state(trx)) {
    csdebug() << __func__ << ": smart new_state transaction[" << trxInd << "] included in consensus";
    if (context.smart_contracts().is_closed_smart_contract(trx.target())) {
      cslog() << __func__ << ": reject smart new_state transaction because related contract is closed";
      rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
      return false;
    }
    csdb::Transaction initTransaction = WalletsCache::findSmartContractInitTrx(trx, context.blockchain());
    if (!initTransaction.is_valid()) {
      cslog() << __func__ << ": reject new_state transaction because related start transaction is not found";
      rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
      return false;    
    }
    csdb::UserField feeField = trx.user_field(trx_uf::new_state::Fee);
    if (!feeField.is_valid()) {
      cslog() << __func__ << ": reject new_state transaction because execution fee is not set properly";
      rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
      return false;
    }
    csdb::Amount feeForExecution(feeField.value<csdb::Amount>());
    if ((csdb::Amount(initTransaction.max_fee().to_double()) - csdb::Amount(initTransaction.counted_fee().to_double()))
       < csdb::Amount(trx.counted_fee().to_double()) + feeForExecution) {
      cslog() << __func__ << ": reject new_state transaction because execution fee is not enough";
      rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
      return false;
    }
    WalletsState::WalletId initTrxId{};
    WalletsState::WalletData& initTrxWallState = walletsState_.getData(initTransaction.source(), initTrxId);
    newBalance = initTrxWallState.balance_
               + csdb::Amount(initTransaction.max_fee().to_double())
               - csdb::Amount(initTransaction.counted_fee().to_double())
               - trx.amount()
               - feeForExecution
               - csdb::Amount(trx.counted_fee().to_double());

    walletsState_.setModified(initTrxId);
  } else {
    if (trx.source() == trx.target()) {
      cslog() << __func__ << ": transaction[" << trxInd << "] rejected, source equals to target";
      return false;
    }
    if (csdb::Amount(trx.max_fee().to_double()) < csdb::Amount(trx.counted_fee().to_double())) {
      cslog() << __func__ << ": transaction[" << trxInd << "] rejected, fee is not enough";
      return false;
    }
    if (SmartContracts::is_executable(trx)) {
      newBalance = wallState.balance_ - trx.amount() - csdb::Amount(trx.max_fee().to_double());
    } else {
      if (smarts.is_known_smart_contract(trx.source())) {
        for (const auto& t : trxs) {
          if (SmartContracts::is_new_state(t) &&
              std::find(rejectedNewStates_.begin(), rejectedNewStates_.end(),
              context.smart_contracts().absolute_address(t.source())) == rejectedNewStates_.end()) {
            auto initTransaction = WalletsCache::findSmartContractInitTrx(t, context.blockchain());
            if (initTransaction.is_valid() &&
                smarts.absolute_address(initTransaction.target()) == smarts.absolute_address(trx.source())) {
              auto it = payableMaxFees_.find(smarts.absolute_address(initTransaction.source()));
              csdb::Amount leftFromMaxFee;
              if (it == payableMaxFees_.end()) {
                leftFromMaxFee = csdb::Amount(initTransaction.max_fee().to_double() - csdb::Amount(trx.counted_fee().to_double()));
              } else {
                leftFromMaxFee = it->second - csdb::Amount(trx.counted_fee().to_double());
              }
              if (leftFromMaxFee < zeroBalance_) {
                cslog() << __func__ << ": smart source transactions rejected, out of max fee from init trx";
                return false;
              }
              payableMaxFees_.insert(std::make_pair(smarts.absolute_address(initTransaction.source()), leftFromMaxFee));
            }
          }
        }
        newBalance = wallState.balance_ - trx.amount();      
      } else {
        if (smarts.is_known_smart_contract(trx.target())) {
          newBalance = wallState.balance_ - trx.amount() - csdb::Amount(trx.max_fee().to_double());
        } else {
          newBalance = wallState.balance_ - trx.amount() - csdb::Amount(trx.counted_fee().to_double());
        }
      }
    }
  }

  wallState.balance_ = newBalance;

  wallState.trxTail_.push(trx.innerID());
  trxList_[trxInd] = wallState.lastTrxInd_;
  wallState.lastTrxInd_ = static_cast<decltype(wallState.lastTrxInd_)>(trxInd);
  walletsState_.setModified(walletId);

  if (wallState.balance_ < zeroBalance_) {
    negativeNodes_.push_back(&wallState);
    if (SmartContracts::is_new_state(trx)) {
      rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
    }
    return false;
  }
  return true;
}

bool TransactionsValidator::validateTransactionAsTarget(const csdb::Transaction& trx) {
  //TODO: test WalletId exists
  WalletsState::WalletId walletId{};
  WalletsState::WalletData& wallState = walletsState_.getData(trx.target(), walletId);

  wallState.balance_ = wallState.balance_ + trx.amount();

  walletsState_.setModified(walletId);
  return true;
}

void TransactionsValidator::checkRejectedSmarts(SolverContext& context, const Transactions& trxs,
                                                CharacteristicMask& maskIncluded) {
  using rejectedSmart = std::pair<csdb::Transaction, size_t>;
  auto& smarts = context.smart_contracts();
  std::vector<csdb::Transaction> newStates;
  std::vector<rejectedSmart> rejectedSmarts;
  size_t maskSize = maskIncluded.size();
  size_t i = 0;

  for (const auto& t : trxs) {
    if (i < maskSize && *(maskIncluded.cbegin() + i) == 0) {
      if (smarts.is_known_smart_contract(t.source())) {
        rejectedSmarts.push_back(std::make_pair(t, i));
      }
    }
    if (i < maskSize && SmartContracts::is_new_state(t) &&
        *(maskIncluded.cbegin() + i) != 0) {
      newStates.push_back(t);
    }
    ++i;
  } 

  for (const auto& state : newStates) {
    csdb::Transaction initTransaction = WalletsCache::findSmartContractInitTrx(state, context.blockchain());
    auto it = std::find_if(rejectedSmarts.cbegin(), rejectedSmarts.cend(),
      [&](const auto& o) { return (smarts.absolute_address(o.first.source()) == smarts.absolute_address(initTransaction.target())); });
    if (it != rejectedSmarts.end()) {
      WalletsState::WalletId walletId{};
      WalletsState::WalletData& wallState = walletsState_.getData(it->first.source(), walletId);
      wallState.balance_ += initTransaction.amount();
      wallState.balance_ += csdb::Amount(it->first.counted_fee().to_double());
      if (wallState.balance_ >= zeroBalance_) {
        maskIncluded[it->second] = 1;
        rejectedSmarts.erase(it);
      }
    }
  }
}

void TransactionsValidator::validateByGraph(CharacteristicMask& maskIncluded, const Transactions& trxs,
                                            csdb::Pool& trxsExcluded) {
  payableMaxFees_.clear();
  rejectedNewStates_.clear();
  while (!negativeNodes_.empty()) {
    Node& currNode = *negativeNodes_.back();
    negativeNodes_.pop_back();

    if (currNode.balance_ >= zeroBalance_) {
      continue;
    }

    removeTransactions(currNode, trxs, maskIncluded, trxsExcluded);
  }
}

void TransactionsValidator::removeTransactions(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded,
                                               csdb::Pool& trxsExcluded) {
  if (removeTransactions_PositiveOne(node, trxs, maskIncluded, trxsExcluded)) {
    return;
  }
  if (removeTransactions_PositiveAll(node, trxs, maskIncluded, trxsExcluded)) {
    return;
  }
  if (removeTransactions_NegativeOne(node, trxs, maskIncluded, trxsExcluded)) {
    return;
  }
  if (removeTransactions_NegativeAll(node, trxs, maskIncluded, trxsExcluded)) {
    return;
  }

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
    const csdb::Amount& trxCost = trx.amount().to_double() + trx.counted_fee().to_double();

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

    node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.counted_fee().to_double());
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

    node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.counted_fee().to_double());
    destNode.balance_ = destNode.balance_ - trx.amount();

    *prevNext = trxList_[trxInd];
    trxList_[trxInd] = WalletsState::noInd_;

    ++cntRemovedTrxs_;

    if (node.balance_ >= zeroBalance_) {
      return true;
    }
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
    const csdb::Amount& trxCost = trx.amount().to_double() + trx.counted_fee().to_double();

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

    node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.counted_fee().to_double());
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

    node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.counted_fee().to_double());
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
