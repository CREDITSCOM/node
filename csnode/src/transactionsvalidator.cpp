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
#include <csnode/nodecore.hpp>
#include <smartcontracts.hpp>
#include <solvercontext.hpp>
#include <walletscache.hpp>

namespace {
//const uint8_t kInvalidMarker = 0;
//const uint8_t kValidMarker = 1;
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

Reject::Reason TransactionsValidator::validateTransaction(SolverContext& context, const Transactions& trxs, size_t trxInd) {
	const auto r = validateTransactionAsSource(context, trxs, trxInd);
    if (r == Reject::Reason::None) {
        return validateTransactionAsTarget(trxs[trxInd]);
    }
    return r;
}

Reject::Reason TransactionsValidator::validateNewStateAsSource(SolverContext& context, const csdb::Transaction& trx) {
    auto& smarts = context.smart_contracts();
    if (smarts.is_closed_smart_contract(trx.target())) {
        cslog() << kLogPrefix << __func__ << ": reject smart new_state transaction, related contract is closed";
        return Reject::Reason::ContractClosed;
    }
    csdb::Transaction initTransaction = SmartContracts::get_transaction(context.blockchain(), trx);
    if (!initTransaction.is_valid()) {
        cslog() << kLogPrefix << __func__ << ": reject new_state transaction, starter transaction does not exist";
        return Reject::Reason::MalformedTransaction;
    }
    csdb::UserField feeField = trx.user_field(trx_uf::new_state::Fee);
    if (!feeField.is_valid()) {
        cslog() << kLogPrefix << __func__ << ": reject new_state transaction, execution fee is not set properly";
        return Reject::Reason::MalformedTransaction;
    }

    csdb::Amount feeForExecution(feeField.value<csdb::Amount>());
    if ((csdb::Amount(initTransaction.max_fee().to_double()) - csdb::Amount(initTransaction.counted_fee().to_double())) <
        csdb::Amount(trx.counted_fee().to_double()) + feeForExecution) {
        cslog() << kLogPrefix << __func__ << ": reject new_state transaction, fee is not enough"
                << "\nInit Transaction max fee = " << initTransaction.max_fee().to_double() << "\nInit Transaction counted fee = " << initTransaction.counted_fee().to_double()
                << "\nNew State transaction counted fee = " << trx.counted_fee().to_double() << "\nNew State transaction exec fee = " << feeForExecution.to_double();
        return Reject::Reason::InsufficientMaxFee;
    }
    WalletsState::WalletData& initTrxWallState = walletsState_.getData(initTransaction.source());
    csdb::Amount newBalance = initTrxWallState.balance_ + csdb::Amount(initTransaction.max_fee().to_double()) - csdb::Amount(initTransaction.counted_fee().to_double()) -
                              trx.amount() - feeForExecution - csdb::Amount(trx.counted_fee().to_double());

    initTrxWallState.balance_ = newBalance;

    if (initTrxWallState.balance_ < zeroBalance_) {
        cslog() << kLogPrefix << __func__ << ": reject new_state transaction, initier is out of funds";
        return Reject::Reason::NegativeResult;
    }
    return Reject::Reason::None;
}

Reject::Reason TransactionsValidator::validateCommonAsSource(SolverContext& context, const Transactions& trxs, size_t trxInd, WalletsState::WalletData& wallState) {
    auto& smarts = context.smart_contracts();
    const auto trx = trxs[trxInd];
    csdb::Amount newBalance;

    if (trx.source() == trx.target()) {
        cslog() << kLogPrefix << __func__ << ": reject transaction[" << trxInd << "], source equals to target";
        return Reject::Reason::SourceIsTarget;
    }

    // max_fee does not matter for new_state and smart emitted:
    bool is_smart_emitted = smarts.is_known_smart_contract(trx.source());
    if (!is_smart_emitted && !SmartContracts::is_new_state(trx)) {
        const double max_fee = trx.max_fee().to_double();
        const double counted_fee = trx.counted_fee().to_double();
        if (csdb::Amount(max_fee) < csdb::Amount(counted_fee)) {
            cslog() << kLogPrefix << __func__ << ": reject transaction[" << trxInd << "], max fee (" << max_fee
                << ") is less than counted fee (" << counted_fee << ")";
            return Reject::Reason::InsufficientMaxFee;
        }
    }

    if (SmartContracts::is_executable(trx)) {
        newBalance = wallState.balance_ - trx.amount() - csdb::Amount(trx.max_fee().to_double());
    }
    else {
        if (is_smart_emitted) {
            auto sourceAbsAddr = smarts.absolute_address(trx.source());
            if (isRejectedSmart(sourceAbsAddr)) {
                csdebug() << kLogPrefix << __func__ << ": reject contract emitted transaction, new_state was rejected.";
                return Reject::Reason::CompleteReject;
            }

            if (validNewStates_.empty()) {
                csdebug() << kLogPrefix << __func__ << ": reject contract emitted transaction, no valid new states in block.";
                return Reject::Reason::MalformedTransaction;
            }

            csdb::Transaction initTransaction = cs::SmartContracts::get_transaction(context.blockchain(), trxs[validNewStates_.back().first]);

            if (initTransaction.is_valid() && smarts.absolute_address(initTransaction.target()) == sourceAbsAddr) {
                auto initerAddr = smarts.absolute_address(initTransaction.source());
                auto it = payableMaxFees_.find(initerAddr);
                csdb::Amount leftFromMaxFee;
                if (it == payableMaxFees_.end()) {
                    leftFromMaxFee = csdb::Amount(initTransaction.max_fee().to_double() - csdb::Amount(trx.counted_fee().to_double()));
                }
                else {
                    leftFromMaxFee = it->second - csdb::Amount(trx.counted_fee().to_double());
                }
                if (leftFromMaxFee < zeroBalance_) {
                    cslog() << kLogPrefix << __func__ << ": reject contract emitted transaction, out of fee in starter transaction";
                    rejectedNewStates_[sourceAbsAddr] = Reject::Reason::InsufficientMaxFee;
                    validNewStates_.back().second = false;
                    return Reject::Reason::InsufficientMaxFee;
                }
                payableMaxFees_[initerAddr] = leftFromMaxFee;
            }
            else {
                cslog() << kLogPrefix << __func__ << ": reject contract emitted transaction, problems with starter transaction";
                rejectedNewStates_[sourceAbsAddr] = Reject::Reason::MalformedTransaction;
                validNewStates_.back().second = false;
                return Reject::Reason::MalformedTransaction;
            }

            newBalance = wallState.balance_ - trx.amount();
            if (newBalance < zeroBalance_) {
                validNewStates_.back().second = false;
                smartsWithNegativeBalances_.push_back(std::make_pair(trx, trxInd));
            }
        }
        else {
            if (smarts.is_known_smart_contract(trx.target())) {
                newBalance = wallState.balance_ - trx.amount() - csdb::Amount(trx.max_fee().to_double());
            }
            else {
                //calculate balance only if transaction isn't de-delegate
                csdb::UserField delegateField = trx.user_field(trx_uf::sp::delegated);
                if (delegateField.is_valid() && delegateField.value<uint64_t>() == 2) {
                    newBalance = wallState.balance_ - csdb::Amount(trx.counted_fee().to_double());
                }
                else {
                    newBalance = wallState.balance_ - trx.amount() - csdb::Amount(trx.counted_fee().to_double());
                }
            }
        }
    }
    if (smarts.is_known_smart_contract(trx.target()) && csdb::Amount(trx.max_fee().to_double()) > wallState.balance_) {
        cslog() << kLogPrefix << "transaction[" << trxInd << "] balance = " << wallState.balance_.to_double() << ", max_fee = " << trx.max_fee().to_double();
    }
    wallState.balance_ = newBalance;
    return Reject::Reason::None;
}

Reject::Reason TransactionsValidator::validateTransactionAsSource(SolverContext& context, const Transactions& trxs, size_t trxInd) {
    const auto& trx = trxs[trxInd];
    WalletsState::WalletData& wallState = walletsState_.getData(trx.source());
	Reject::Reason r = Reject::Reason::None;

    if (!wallState.trxTail_.isAllowed(trx.innerID())) {
		r = wallState.trxTail_.isDuplicated(trx.innerID()) ? Reject::Reason::DuplicatedInnerID : Reject::Reason::DisabledInnerID;
		if (r == Reject::Reason::DuplicatedInnerID) {
			csdebug() << kLogPrefix << "reject transaction, duplicated innerID " << trx.innerID();
		}
		else {
			csdebug() << kLogPrefix << "reject transaction, disabled innerID " << trx.innerID() << ", allowed unique in " << wallState.trxTail_.printRange();
		}
        if (SmartContracts::is_new_state(trx)) {
            auto addr = context.smart_contracts().absolute_address(trx.source());
            saveNewState(addr, trxInd, r);
            duplicatedNewStates_.insert(addr);
        }
        return r;
    }

    if (SmartContracts::is_new_state(trx)) {
        csdebug() << kLogPrefix << __func__ << ": smart new_state transaction[" << trxInd << "] included in consensus";
        auto absAddr = context.smart_contracts().absolute_address(trx.source());
		if (isRejectedSmart(absAddr)) {
			r = getRejectReason(absAddr);
		}
		else {
			r = validateNewStateAsSource(context, trx);
		}
        saveNewState(absAddr, trxInd, r);
    }
    else {
		r = validateCommonAsSource(context, trxs, trxInd, wallState);
    }

	if (r != Reject::Reason::None) {
		return r;
	}

    if (wallState.balance_ < zeroBalance_ && !SmartContracts::is_new_state(trx)) {
        csdebug() << kLogPrefix << "transaction[" << trxInd << "] results to potentially negative balance " << wallState.balance_.to_double();
        // will be checked in rejected smarts
        if (context.smart_contracts().is_known_smart_contract(trx.source())) {
            return Reject::Reason::NegativeResult;
        }
        // will be validated by graph
        negativeNodes_.push_back(&wallState);
    }
    csdb::UserField delegateField = trx.user_field(trx_uf::sp::delegated);
    if (delegateField.is_valid()) {
        WalletsState::WalletData& wallTargetState = walletsState_.getData(trx.target());
        auto tKey = trx.target().is_public_key() ? trx.target().public_key() : context.blockchain().getCacheUpdater().toPublicKey(trx.target());
        auto itTarget = wallState.delegateTargets_.find(tKey);
        auto sKey = trx. source().is_public_key() ? trx.source().public_key() : context.blockchain().getCacheUpdater().toPublicKey(trx.source());
        auto itSource = wallTargetState.delegateSources_.find(sKey);
        if (delegateField.value<uint64_t>() == trx_uf::sp::de::legate || delegateField.value<uint64_t>() >= trx_uf::sp::de::legate_min_utc) {
             if (trx.amount() < Consensus::MinStakeDelegated) {
                csdebug() << kLogPrefix << "The delegated amount is too low";
                return Reject::Reason::AmountTooLow;
            }
            //if (wallTargetState.delegated_ > csdb::Amount{ 0 }) {
            //    csdetails() << kLogPrefix << "Can't delegate to the account that was already delegated";
            //    return Reject::Reason::AlreadyDelegated;
            //}
            if (context.smart_contracts().is_known_smart_contract(trx.target())) {
                csdebug() << kLogPrefix << "Target can't be a smart contract";
                return Reject::Reason::IncorrectTarget;
            }
        }
        else if (delegateField.value<uint64_t>() == trx_uf::sp::de::legated_withdraw) {
            if (itTarget == wallState.delegateTargets_.end()) {
                csdebug() << kLogPrefix << "No such target delegate in source account state";
                return Reject::Reason::IncorrectTarget;
            }
            else {

                if (itSource == wallTargetState.delegateSources_.end()) {
                    cserror() << kLogPrefix << "No such delegate source in target records";
                    return Reject::Reason::IncorrectTarget;
                }
                else {
                    auto itt = std::find_if(itTarget->second.begin(), itTarget->second.end(), [](cs::TimeMoney& tm) {return tm.time == cs::Zero::timeStamp; });
                    auto its = std::find_if(itSource->second.begin(), itSource->second.end(), [](cs::TimeMoney& tm) {return tm.time == cs::Zero::timeStamp; });
                    bool itt_found = itt != itTarget->second.end();
                    bool its_found = its != itSource->second.end();
                    if (its_found && itt_found) {
                        if (itt->amount != its->amount) {
                            cserror() << kLogPrefix << "The sum of delegation is not properly set to the sender and target accounts";
                            return Reject::Reason::MalformedDelegation;
                        }
                        else {
                            if (itt->amount < trx.amount()) {
                                csdebug() << kLogPrefix << "The sum of delegation isn't enough for this transaction";
                                return Reject::Reason::IncorrectSum;
                            }
                        }
                    }
                    else if (its_found != itt_found) {
                        cserror() << kLogPrefix << "Delegation is corrupted, unable to proceed";
                        return Reject::Reason::MalformedDelegation;
                    }
                    else {
                        cserror() << kLogPrefix << "No delegation to withdraw found";
                        return Reject::Reason::MalformedDelegation;
                    }
                }
            }
        }
        else {
            csdetails() << kLogPrefix << "not specified transaction field";
            return Reject::Reason::MalformedTransaction;
        }
    }

    wallState.trxTail_.push(trx.innerID());
    csdetails() << kLogPrefix << "innerID of " << cs::SmartContracts::to_base58(context.blockchain(), trx.source()) << " <- " << trx.innerID();
    trxList_[trxInd] = wallState.lastTrxInd_;
    wallState.lastTrxInd_ = static_cast<decltype(wallState.lastTrxInd_)>(trxInd);

    return Reject::Reason::None;
}

Reject::Reason TransactionsValidator::validateTransactionAsTarget(const csdb::Transaction& trx) {
    WalletsState::WalletData& wallState = walletsState_.getData(trx.target());
    //TODO insert delegated check code here
    wallState.balance_ = wallState.balance_ + trx.amount();

    return Reject::Reason::None;
}

size_t TransactionsValidator::checkRejectedSmarts(SolverContext& context, const Transactions& trxs, CharacteristicMask& maskIncluded) {
    auto& smarts = context.smart_contracts();
    size_t restoredCounter = 0;

    std::map<csdb::Address, csdb::Amount> smartBalances;

    for (auto& state : validNewStates_) {
        auto contract_abs_addr = smarts.absolute_address(trxs[state.first].source());

        if (isRejectedSmart(contract_abs_addr)) { // rejected due to fee, signature or problems with new state
            continue;
        }

        auto it = std::find_if(smartsWithNegativeBalances_.cbegin(), smartsWithNegativeBalances_.cend(),
                               [&](const auto& o) { return (smarts.absolute_address(o.first.source()) == contract_abs_addr) && o.second > state.first; });

        if (it == smartsWithNegativeBalances_.end()) {
            continue;
        }

        WalletsState::WalletData& wallState = walletsState_.getData(contract_abs_addr);
        csdb::Transaction initTransaction = SmartContracts::get_transaction(context.blockchain(), trxs[state.first]);
        wallState.balance_ += initTransaction.amount();

        csdb::Amount availableForSpend = initTransaction.amount();
        size_t prevIndex = it->second;

        while (it != smartsWithNegativeBalances_.end()) {
            if (it->second - prevIndex > 1) {
                break;
            }
            prevIndex = it->second;
            availableForSpend -= it->first.amount();
            ++it;
        }

        if (availableForSpend >= zeroBalance_) {
            state.second = true;
        }

        if (wallState.balance_ >= zeroBalance_) {
            restoredCounter += makeSmartsValid(context, smartsWithNegativeBalances_, contract_abs_addr, maskIncluded);
        }

        smartBalances[contract_abs_addr] = wallState.balance_;
    }

    std::set<csdb::Address> uniqueInvalidatedStates;

    for (auto& state : validNewStates_) {
        auto contract_abs_addr = smarts.absolute_address(trxs[state.first].source());

        if (isRejectedSmart(contract_abs_addr) && getRejectReason(contract_abs_addr) != Reject::Reason::NegativeResult) {
            continue;
        }

        if (smartBalances[contract_abs_addr] >= zeroBalance_) {
            state.second = true;
        }
        else {
            saveNewState(contract_abs_addr, state.first, Reject::Reason::NegativeResult);
            if (!state.second && !uniqueInvalidatedStates.insert(contract_abs_addr).second) {
                state.second = true;
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
            maskIncluded[smarts[i].second] = Reject::Reason::None;
            ++restoredCounter;
            csdebug() << kLogPrefix << "source of transation[" << smarts[i].second << "] is replenished, cancel reject";

            WalletsState::WalletData& wallState = walletsState_.getData(smarts[i].first.source());
            wallState.trxTail_.push(smarts[i].first.innerID());
            trxList_[smarts[i].second] = wallState.lastTrxInd_;
            wallState.lastTrxInd_ = static_cast<decltype(wallState.lastTrxInd_)>(smarts[i].second);
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

        Node& destNode = walletsState_.getData(trx.target());

        const bool isTrxPositive = (trx.amount() <= destNode.balance_);

        if (!isTrxPositive) {
            prevNext = &trxList_[trxInd];
            continue;
        }

        maskIncluded[trxInd] = Reject::Reason::NegativeResult;

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

        Node& destNode = walletsState_.getData(trx.target());
        const bool isTrxPositive = (trx.amount() <= destNode.balance_);

        if (!isTrxPositive) {
            prevNext = &trxList_[trxInd];
            continue;
        }

        maskIncluded[trxInd] = Reject::Reason::NegativeResult;

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

        Node& destNode = walletsState_.getData(trx.target());

        maskIncluded[trxInd] = Reject::Reason::NegativeResult;

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

        Node& destNode = walletsState_.getData(trx.target());

        maskIncluded[trxInd] = Reject::Reason::NegativeResult;

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

bool TransactionsValidator::duplicatedNewState(SolverContext& context, const csdb::Address& addr) const {
    auto abs_addr = context.smart_contracts().absolute_address(addr);
    return duplicatedNewStates_.find(abs_addr) != duplicatedNewStates_.end();
}
}  // namespace cs
