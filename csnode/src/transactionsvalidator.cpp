#include <csnode/transactionsvalidator.hpp>

#include <map>
#include <vector>
#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <client/params.hpp>
#include <csdb/amount.hpp>
#include <csdb/amount_commission.hpp>
#include <lib/system/logger.hpp>
#include <smartcontracts.hpp>
#include <solvercontext.hpp>
#include <walletscache.hpp>

namespace {
const uint8_t kInvalidMarker = 0;
const uint8_t kValidMarker = 1;
const char* kLogPrefix = "Validator: ";
}  // namespace

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

bool TransactionsValidator::validateTransaction(SolverContext& context, const Transactions& trxs, size_t trxInd) {
    if (!validateTransactionAsSource(context, trxs, trxInd)) {
        return false;
    }
    return validateTransactionAsTarget(trxs[trxInd]);
}

bool TransactionsValidator::validateNewStateAsSource(SolverContext& context, const csdb::Transaction& trx) {
    auto& smarts = context.smart_contracts();
    if (smarts.is_closed_smart_contract(trx.target())) {
        cslog() << kLogPrefix << __func__ << ": reject smart new_state transaction, related contract is closed";
        rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
        return false;
    }
    csdb::Transaction initTransaction = SmartContracts::get_transaction(context.blockchain(), trx);
    if (!initTransaction.is_valid()) {
        cslog() << kLogPrefix << __func__ << ": reject new_state transaction, starter transaction does not exist";
        rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
        return false;
    }
    csdb::UserField feeField = trx.user_field(trx_uf::new_state::Fee);
    if (!feeField.is_valid()) {
        cslog() << kLogPrefix << __func__ << ": reject new_state transaction, execution fee is not set properly";
        rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
        return false;
    }
    csdb::Amount feeForExecution(feeField.value<csdb::Amount>());
    if ((csdb::Amount(initTransaction.max_fee().to_double()) - csdb::Amount(initTransaction.counted_fee().to_double())) <
        csdb::Amount(trx.counted_fee().to_double()) + feeForExecution) {
        cslog() << kLogPrefix << __func__ << ": reject new_state transaction, fee is not enough"
                << "\nInit Transaction max fee = " << initTransaction.max_fee().to_double() << "\nInit Transaction counted fee = " << initTransaction.counted_fee().to_double()
                << "\nNew State transaction counted fee = " << trx.counted_fee().to_double() << "\nNew State transaction exec fee = " << feeForExecution.to_double();
        rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
        return false;
    }
    WalletsState::WalletId initTrxId{};
    WalletsState::WalletData& initTrxWallState = walletsState_.getData(initTransaction.source(), initTrxId);
    csdb::Amount newBalance = initTrxWallState.balance_ + csdb::Amount(initTransaction.max_fee().to_double()) - csdb::Amount(initTransaction.counted_fee().to_double()) -
                              trx.amount() - feeForExecution - csdb::Amount(trx.counted_fee().to_double());

    initTrxWallState.balance_ = newBalance;
    walletsState_.setModified(initTrxId);

    if (initTrxWallState.balance_ < zeroBalance_) {
        cslog() << kLogPrefix << __func__ << ": reject new_state transaction, initier is out of funds";
        rejectedNewStates_.push_back(smarts.absolute_address(trx.source()));
        return false;
    }
    return true;
}

bool TransactionsValidator::validateCommonAsSource(SolverContext& context, const Transactions& trxs, size_t trxInd, WalletsState::WalletData& wallState) {
    auto& smarts = context.smart_contracts();
    const auto trx = trxs[trxInd];
    csdb::Amount newBalance;

    if (trx.source() == trx.target()) {
        cslog() << kLogPrefix << __func__ << ": reject transaction[" << trxInd << "], source equals to target";
        return false;
    }
    const double max_fee = trx.max_fee().to_double();
    const double counted_fee = trx.counted_fee().to_double();
    if (csdb::Amount(max_fee) < csdb::Amount(counted_fee)) {
        cslog() << kLogPrefix << __func__ << ": reject transaction[" << trxInd << "], max fee (" << max_fee
            << ") is less than counted fee (" << counted_fee << ")";
        return false;
    }

    if (SmartContracts::is_executable(trx)) {
        newBalance = wallState.balance_ - trx.amount() - csdb::Amount(trx.max_fee().to_double());
    }
    else {
        if (smarts.is_known_smart_contract(trx.source())) {
            bool ok = false;
            for (const auto& t : trxs) {
                if (SmartContracts::is_new_state(t) &&
                    std::find(rejectedNewStates_.begin(), rejectedNewStates_.end(), context.smart_contracts().absolute_address(t.source())) == rejectedNewStates_.end()) {
                    csdb::Transaction initTransaction = cs::SmartContracts::get_transaction(context.blockchain(), t);
                    if (initTransaction.is_valid() && smarts.absolute_address(initTransaction.target()) == smarts.absolute_address(trx.source())) {
                        auto it = payableMaxFees_.find(smarts.absolute_address(initTransaction.source()));
                        csdb::Amount leftFromMaxFee;
                        if (it == payableMaxFees_.end()) {
                            leftFromMaxFee = csdb::Amount(initTransaction.max_fee().to_double() - csdb::Amount(trx.counted_fee().to_double()));
                        }
                        else {
                            leftFromMaxFee = it->second - csdb::Amount(trx.counted_fee().to_double());
                        }
                        if (leftFromMaxFee < zeroBalance_) {
                            cslog() << kLogPrefix << __func__ << ": reject contract emitted transaction, out of fee in starter transaction";
                            return false;
                        }
                        ok = true;
                        payableMaxFees_.insert(std::make_pair(smarts.absolute_address(initTransaction.source()), leftFromMaxFee));
                    }
                }
            }
            if (!ok) {
                cslog() << kLogPrefix << __func__ << ": reject contract emitted transaction, new_state not found in block";
                return false;
            }
            newBalance = wallState.balance_ - trx.amount();
        }
        else {
            if (smarts.is_known_smart_contract(trx.target())) {
                newBalance = wallState.balance_ - trx.amount() - csdb::Amount(trx.max_fee().to_double());
            }
            else {
                newBalance = wallState.balance_ - trx.amount() - csdb::Amount(trx.counted_fee().to_double());
            }
        }
    }
    if (smarts.is_known_smart_contract(trx.target()) && csdb::Amount(trx.max_fee().to_double()) > wallState.balance_) {
        cslog() << kLogPrefix << "transaction[" << trxInd << "] balance = " << wallState.balance_.to_double() << ", max_fee = " << trx.max_fee().to_double();
    }
    wallState.balance_ = newBalance;
    return true;
}

bool TransactionsValidator::validateTransactionAsSource(SolverContext& context, const Transactions& trxs, size_t trxInd) {
    const auto& trx = trxs[trxInd];
    WalletsState::WalletId walletId{};
    WalletsState::WalletData& wallState = walletsState_.getData(trx.source(), walletId);
    walletsState_.setModified(walletId);

    if (!wallState.trxTail_.isAllowed(trx.innerID())) {
        csdebug() << kLogPrefix << "reject transaction, duplicated or incorrect innerID " << trx.innerID() << ", allowed " << wallState.trxTail_.printRange();
        if (SmartContracts::is_new_state(trx)) {
            rejectedNewStates_.push_back(context.smart_contracts().absolute_address(trx.source()));
        }
        return false;
    }

    if (SmartContracts::is_new_state(trx)) {
        csdebug() << kLogPrefix << __func__ << ": smart new_state transaction[" << trxInd << "] included in consensus";
        if (!validateNewStateAsSource(context, trx)) {
            return false;
        }
    }
    else {
        if (!validateCommonAsSource(context, trxs, trxInd, wallState)) {
            return false;
        }
    }

    if (wallState.balance_ < zeroBalance_ && !SmartContracts::is_new_state(trx)) {
        csdebug() << kLogPrefix << "transaction[" << trxInd << "] results to potentially negative balance " << wallState.balance_.to_double();
        // will be checked in rejected smarts
        if (context.smart_contracts().is_known_smart_contract(trx.source())) {
            return false;
        }
        // will be validated by graph
        negativeNodes_.push_back(&wallState);
    }

    wallState.trxTail_.push(trx.innerID());
    csdetails() << kLogPrefix << "innerID of " << cs::SmartContracts::to_base58(context.blockchain(), trx.source()) << " <- " << trx.innerID();
    trxList_[trxInd] = wallState.lastTrxInd_;
    wallState.lastTrxInd_ = static_cast<decltype(wallState.lastTrxInd_)>(trxInd);

    return true;
}

bool TransactionsValidator::validateTransactionAsTarget(const csdb::Transaction& trx) {
    WalletsState::WalletId walletId{};
    WalletsState::WalletData& wallState = walletsState_.getData(trx.target(), walletId);

    wallState.balance_ = wallState.balance_ + trx.amount();

    walletsState_.setModified(walletId);
    return true;
}

size_t TransactionsValidator::checkRejectedSmarts(SolverContext& context, const Transactions& trxs, CharacteristicMask& maskIncluded) {
    using rejectedSmart = std::pair<csdb::Transaction, size_t>;
    auto& smarts = context.smart_contracts();
    std::vector<csdb::Transaction> newStates;
    std::vector<rejectedSmart> rejectedSmarts;
    size_t maskSize = maskIncluded.size();
    size_t i = 0;
    size_t restoredCounter = 0;

    for (const auto& t : trxs) {
        if (i < maskSize && smarts.is_known_smart_contract(t.source()) && !SmartContracts::is_new_state(t)) {
            WalletsState::WalletId id{};
            WalletsState::WalletData& wallState = walletsState_.getData(t.source(), id);
            if (wallState.balance_ < zeroBalance_) {
                rejectedSmarts.push_back(std::make_pair(t, i));
            }
        }
        else if (i < maskSize && SmartContracts::is_new_state(t) && *(maskIncluded.cbegin() + i) == kValidMarker) {
            newStates.push_back(t);
        }
        ++i;
    }

    for (const auto& state : newStates) {
        csdb::Transaction initTransaction = SmartContracts::get_transaction(context.blockchain(), state);
        auto it = std::find_if(rejectedSmarts.cbegin(), rejectedSmarts.cend(),
                               [&](const auto& o) { return (smarts.absolute_address(o.first.source()) == smarts.absolute_address(initTransaction.target())); });
        if (it != rejectedSmarts.end()) {
            WalletsState::WalletId walletId{};
            WalletsState::WalletData& wallState = walletsState_.getData(it->first.source(), walletId);
            wallState.balance_ += initTransaction.amount();
            if (wallState.balance_ >= zeroBalance_) {
                restoredCounter += makeSmartsValid(context, rejectedSmarts, it->first.source(), maskIncluded);
            }
        }
    }

    return restoredCounter;
}

size_t TransactionsValidator::makeSmartsValid(SolverContext& context, RejectedSmarts& smarts, const csdb::Address& source, CharacteristicMask& maskIncluded) {
    size_t maskSize = maskIncluded.size();
    auto& s = context.smart_contracts();
    size_t restoredCounter = 0;
    for (size_t i = 0; i < smarts.size(); ++i) {
        if (s.absolute_address(smarts[i].first.source()) == s.absolute_address(source) && smarts[i].second < maskSize) {
            maskIncluded[smarts[i].second] = kValidMarker;
            ++restoredCounter;
            csdebug() << kLogPrefix << "balance of transation[" << smarts[i].second << "] source is replenished by other transaction";

            WalletsState::WalletId walletId{};
            WalletsState::WalletData& wallState = walletsState_.getData(smarts[i].first.source(), walletId);
            wallState.trxTail_.push(smarts[i].first.innerID());
            trxList_[smarts[i].second] = wallState.lastTrxInd_;
            wallState.lastTrxInd_ = static_cast<decltype(wallState.lastTrxInd_)>(smarts[i].second);
            walletsState_.setModified(walletId);
        }
    }
    return restoredCounter;
}

void TransactionsValidator::validateByGraph(SolverContext& context, CharacteristicMask& maskIncluded, const Transactions& trxs) {
    while (!negativeNodes_.empty()) {
        Node& currNode = *negativeNodes_.back();
        negativeNodes_.pop_back();

        if (currNode.balance_ >= zeroBalance_) {
            continue;
        }

        removeTransactions(context, currNode, trxs, maskIncluded);
    }
}

void TransactionsValidator::removeTransactions(SolverContext& context, Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded) {
    if (removeTransactions_PositiveOne(context, node, trxs, maskIncluded)) {
        return;
    }
    if (removeTransactions_PositiveAll(context, node, trxs, maskIncluded)) {
        return;
    }
    if (removeTransactions_NegativeOne(context, node, trxs, maskIncluded)) {
        return;
    }
    if (removeTransactions_NegativeAll(context, node, trxs, maskIncluded)) {
        return;
    }

    csdebug() << "removeTransactions: Failed to make balance non-negative ";
}

bool TransactionsValidator::removeTransactions_PositiveOne(SolverContext& context, Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded) {
    if (node.balance_ >= zeroBalance_)
        return true;

    const csdb::Amount absBalance = -node.balance_;
    TransactionIndex* prevNext = &node.lastTrxInd_;
    auto& smarts = context.smart_contracts();

    for (TransactionIndex trxInd = *prevNext; trxInd != WalletsState::noInd_; trxInd = *prevNext) {
        const csdb::Transaction& trx = trxs[trxInd];

        csdb::Amount trxCost = trx.amount().to_double();
        if (smarts.is_known_smart_contract(trx.target())) {
            trxCost += csdb::Amount(trx.max_fee().to_double());
        }
        else {
            trxCost += csdb::Amount(trx.counted_fee().to_double());
        }

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

        maskIncluded[trxInd] = kInvalidMarker;

        if (smarts.is_known_smart_contract(trx.target())) {
            node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.max_fee().to_double());
        }
        else {
            node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.counted_fee().to_double());
        }
        destNode.balance_ = destNode.balance_ - trx.amount();

        *prevNext = trxList_[trxInd];
        trxList_[trxInd] = WalletsState::noInd_;

        ++cntRemovedTrxs_;

        return true;
    }

    return false;
}

bool TransactionsValidator::removeTransactions_PositiveAll(SolverContext& context, Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded) {
    if (node.balance_ >= zeroBalance_)
        return true;

    TransactionIndex* prevNext = &node.lastTrxInd_;
    auto& smarts = context.smart_contracts();

    for (TransactionIndex trxInd = *prevNext; trxInd != WalletsState::noInd_; trxInd = *prevNext) {
        const csdb::Transaction& trx = trxs[trxInd];

        WalletsState::WalletId walletId{};
        Node& destNode = walletsState_.getData(trx.target(), walletId);
        const bool isTrxPositive = (trx.amount() <= destNode.balance_);

        if (!isTrxPositive) {
            prevNext = &trxList_[trxInd];
            continue;
        }

        maskIncluded[trxInd] = kInvalidMarker;

        if (smarts.is_known_smart_contract(trx.target())) {
            node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.max_fee().to_double());
        }
        else {
            node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.counted_fee().to_double());
        }
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

bool TransactionsValidator::removeTransactions_NegativeOne(SolverContext& context, Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded) {
    if (node.balance_ >= zeroBalance_)
        return true;

    const csdb::Amount absBalance = -node.balance_;
    TransactionIndex* prevNext = &node.lastTrxInd_;
    auto& smarts = context.smart_contracts();

    for (TransactionIndex trxInd = *prevNext; trxInd != WalletsState::noInd_; trxInd = *prevNext) {
        const csdb::Transaction& trx = trxs[trxInd];
        csdb::Amount trxCost = trx.amount().to_double();
        if (smarts.is_known_smart_contract(trx.target())) {
            trxCost += csdb::Amount(trx.max_fee().to_double());
        }
        else {
            trxCost += csdb::Amount(trx.counted_fee().to_double());
        }

        if (trxCost < absBalance) {
            prevNext = &trxList_[trxInd];
            continue;
        }

        WalletsState::WalletId walletId{};
        Node& destNode = walletsState_.getData(trx.target(), walletId);

        maskIncluded[trxInd] = kInvalidMarker;

        if (smarts.is_known_smart_contract(trx.target())) {
            node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.max_fee().to_double());
        }
        else {
            node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.counted_fee().to_double());
        }
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

bool TransactionsValidator::removeTransactions_NegativeAll(SolverContext& context, Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded) {
    if (node.balance_ >= zeroBalance_)
        return true;

    TransactionIndex* prevNext = &node.lastTrxInd_;
    auto& smarts = context.smart_contracts();

    for (TransactionIndex trxInd = *prevNext; trxInd != WalletsState::noInd_; trxInd = *prevNext) {
        const csdb::Transaction& trx = trxs[trxInd];

        WalletsState::WalletId walletId{};
        Node& destNode = walletsState_.getData(trx.target(), walletId);

        maskIncluded[trxInd] = kInvalidMarker;

        if (smarts.is_known_smart_contract(trx.target())) {
            node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.max_fee().to_double());
        }
        else {
            node.balance_ = node.balance_ + trx.amount() + csdb::Amount(trx.counted_fee().to_double());
        }
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
