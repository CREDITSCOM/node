#include <csnode/itervalidator.hpp>

#include <cstring>

#include <csdb/amount_commission.hpp>
#include <csnode/fee.hpp>
#include <csnode/walletsstate.hpp>
#include <smartcontracts.hpp>
#include <solvercontext.hpp>
#include <net/packetvalidator.hpp>

namespace {
const char* kLogPrefix = "Validator: ";
//const uint8_t kInvalidMarker = 0;
//const uint8_t kValidMarker = 1;
}  // namespace

namespace cs {

IterValidator::IterValidator(WalletsState& wallets) {
    pTransval_ = std::make_unique<TransactionsValidator>(wallets, TransactionsValidator::Config{});
}

Characteristic IterValidator::formCharacteristic(SolverContext& context, Transactions& transactions, PacketsVector& smartsPackets) {
    cs::Characteristic characteristic;
    characteristic.mask.resize(transactions.size(), Reject::Reason::None);

    checkTransactionsSignatures(context, transactions, characteristic.mask, smartsPackets);

    bool needNewIteration = false;
    size_t iterationCounter = 1;

    do {
        csdebug() << kLogPrefix << "current iteration: " << iterationCounter;
        context.blockchain().setTransactionsFees(transactions, characteristic.mask);
        context.wallets().updateFromSource();
        pTransval_->reset(transactions.size());
        needNewIteration = validateTransactions(context, characteristic.mask, transactions);
        ++iterationCounter;
    } while (needNewIteration);

    checkRejectedSmarts(context, characteristic.mask, transactions);
    pTransval_->clearCaches();

    return characteristic;
}

void IterValidator::normalizeCharacteristic(Characteristic& inout) const {
    // transform characteristic to its "canonical" form
    if (inout.mask.empty()) {
        return;
    }
    std::for_each(inout.mask.begin(), inout.mask.end(), [](cs::Byte& item) {
        if (item == Reject::Reason::None) {
            item = 1;
        }
        else {
            item = 0;
        }
    });
}

void IterValidator::checkRejectedSmarts(SolverContext& context, cs::Bytes& characteristicMask, const Transactions& transactions) {
    std::vector<SolverContext::RefExecution> rejectList;
    std::set<csdb::Address> firstRejected;

    auto tryAddToRejected = [this, &rejectList, &firstRejected, &context](const csdb::Transaction& tr, const csdb::Address& absAddr) mutable {
        if (!pTransval_->duplicatedNewState(context, tr.source()) && firstRejected.insert(absAddr).second) {
            csdb::UserField fld = tr.user_field(trx_uf::new_state::RefStart);
            if (fld.is_valid()) {
                SmartContractRef ref(fld);
                rejectList.emplace_back(std::make_pair(ref.sequence, static_cast<uint32_t>(ref.transaction)));
            }
        }
    };

    for (auto it = pTransval_->getValidNewStates().rbegin(); it != pTransval_->getValidNewStates().rend(); ++it) {
        if (it->second) {
            continue;
        }

        auto& t = transactions[it->first];
        auto absAddr = context.smart_contracts().absolute_address(t.source());

        if (pTransval_->isRejectedSmart(absAddr)) {
            tryAddToRejected(t, absAddr);
        }
    }

	// normally, transactions.size() == characteristicMask.size()
	const size_t cnt = std::min(transactions.size(), characteristicMask.size());
    for (size_t i = 0; i < cnt; ++i) {
        auto absAddr = context.smart_contracts().absolute_address(transactions[i].source());
        bool valid = characteristicMask[i] == Reject::Reason::None;

        if (!valid && SmartContracts::is_new_state(transactions[i])) {
            tryAddToRejected(transactions[i], absAddr);
        }
        else if (valid && pTransval_->isRejectedSmart(absAddr)) {
            characteristicMask[i] = pTransval_->getRejectReason(absAddr);
            csdebug() << kLogPrefix << "transaction[" << i << "] is rejected contract: "
                << Reject::to_string((Reject::Reason)characteristicMask[i]);
        }
    }

    if (!rejectList.empty()) {
        cslog() << kLogPrefix << "reject " << rejectList.size() << " new_state(s) of contract(s)";
        for (const auto& item : rejectList) {
            cslog() << kLogPrefix << "rejected items in call " << FormatRef(item.first)
                << " start from " << FormatRef(item.first, item.second);
        }
        context.send_rejected_smarts(rejectList);
    }
}

bool IterValidator::validateTransactions(SolverContext& context, cs::Bytes& characteristicMask, const Transactions& transactions) {
    bool needOneMoreIteration = false;
    const size_t transactionsCount = transactions.size();
    size_t blockedCounter = 0;

    // validate each transaction
    for (size_t i = 0; i < transactionsCount; ++i) {
        if (characteristicMask[i] != Reject::Reason::None) {
            continue;
        }

        const csdb::Transaction& transaction = transactions[i];
		Reject::Reason r = pTransval_->validateTransaction(context, transactions, i);

        if (r == Reject::Reason::None && SmartContracts::is_deploy(transaction)) {
            r = deployAdditionalCheck(context, i, transaction);
        }

        if (r != Reject::Reason::None) {
            csdebug() << kLogPrefix << "transaction[" << i << "] rejected by validator";
            characteristicMask[i] = r;
            needOneMoreIteration = true;
            ++blockedCounter;
        }
		// has already set properly
        //else {
        //    characteristicMask[i] = Reject::Reason::None;
        //}
    }

    // validation of all transactions by graph
    size_t restoredCounter = pTransval_->checkRejectedSmarts(context, transactions, characteristicMask);
    if (blockedCounter == restoredCounter) {
        needOneMoreIteration = false;
    }
    pTransval_->validateByGraph(context, characteristicMask, transactions);

    if (pTransval_->getCntRemovedTrxsByGraph() > 0) {
        cslog() << kLogPrefix << "num of trxs rejected by graph validation - " << pTransval_->getCntRemovedTrxsByGraph();
        needOneMoreIteration = true;
    }

    needOneMoreIteration = false; // iterations switched off

    return needOneMoreIteration;
}

Reject::Reason IterValidator::deployAdditionalCheck(SolverContext& context, size_t trxInd, const csdb::Transaction& transaction) {
    // test with get_valid_smart_address() only for deploy transactions
    auto sci = context.smart_contracts().get_smart_contract(transaction);
    if (sci.has_value() && sci.value().method.empty()) {  // is deploy
        csdb::Address deployer_abs_addr = context.blockchain().getAddressByType(transaction.source(), BlockChain::AddressType::PublicKey);
		csdb::Address contract_addr = SmartContracts::get_valid_smart_address(deployer_abs_addr, transaction.innerID(), sci.value().smartContractDeploy);
		if (!(contract_addr == transaction.target())) {
			cslog() << kLogPrefix << ": transaction[" << trxInd << "] rejected, malformed contract address";
			return Reject::Reason::MalformedContractAddress;
		}
    }

    return Reject::Reason::None;
}

void IterValidator::checkTransactionsSignatures(SolverContext& context, const Transactions& transactions, cs::Bytes& characteristicMask, PacketsVector& smartsPackets) {
    checkSignaturesSmartSource(context, smartsPackets);
    size_t transactionsCount = transactions.size();
    size_t maskSize = characteristicMask.size();
    size_t rejectedCounter = 0;
    for (size_t i = 0; i < transactionsCount; ++i) {
        if (i < maskSize) {
            bool correctSignature = checkTransactionSignature(context, transactions[i]);
            if (!correctSignature) {
                characteristicMask[i] = Reject::Reason::WrongSignature;
                rejectedCounter++;
                cslog() << kLogPrefix << "transaction[" << i << "] rejected, incorrect signature.";
                if (SmartContracts::is_new_state(transactions[i])) {
                    pTransval_->saveNewState(context.smart_contracts().absolute_address(transactions[i].source()), i, Reject::Reason::WrongSignature);
                }
            }
        }
    }
    if (rejectedCounter) {
        cslog() << kLogPrefix << "wrong signatures num: " << rejectedCounter;
    }
}

bool IterValidator::checkTransactionSignature(SolverContext& context, const csdb::Transaction& transaction) {
    csdb::Address src = transaction.source();
    // TODO: is_known_smart_contract() does not recognize not yet deployed contract, so all transactions emitted in constructor
    // currently will be rejected
    bool smartSourceTransaction = false;
    bool isSmart = SmartContracts::is_smart_contract(transaction);
    if (!isSmart) {
        smartSourceTransaction = context.smart_contracts().is_known_smart_contract(transaction.source());
    }
    if (!SmartContracts::is_new_state(transaction) && !smartSourceTransaction) {
        if (src.is_wallet_id()) {
            auto pub = context.blockchain().getAddressByType(src, BlockChain::AddressType::PublicKey);
            const auto& starter_key = cs::PacketValidator::instance().getStarterKey();
            if (context.blockchain().isSpecial(transaction) && pub.public_key() != starter_key) {
                return false;
            }
            return transaction.verify_signature(pub.public_key());
        }
        return transaction.verify_signature(src.public_key());
    }
    else {
        // special rule for new_state transactions
        if (SmartContracts::is_new_state(transaction) && src != transaction.target()) {
            csdebug() << kLogPrefix << "smart state transaction has different source and target";
            return false;
        }
        auto it = smartSourceInvalidSignatures_.find(transaction.source());
        if (it != smartSourceInvalidSignatures_.end()) {
            csdebug() << kLogPrefix << "smart contract transaction has invalid signature";
            return false;
        }
        return true;
    }
}

void IterValidator::checkSignaturesSmartSource(SolverContext& context, cs::PacketsVector& smartContractsPackets) {
    smartSourceInvalidSignatures_.clear();

    for (auto& smartContractPacket : smartContractsPackets) {
        if (smartContractPacket.transactions().size() > 0) {
            smartContractPacket.makeHash();
            const auto& transaction = smartContractPacket.transactions()[0];
            SmartContractRef smartRef;
            if (SmartContracts::is_new_state(transaction)) {
                smartRef.from_user_field(transaction.user_field(trx_uf::new_state::RefStart));
            }
            else {
                smartRef.from_user_field(transaction.user_field(trx_uf::smart_gen::RefStart));
            }
            if (!smartRef.is_valid()) {
                cslog() << kLogPrefix << "SmartContractRef is not properly set in transaction";
                smartSourceInvalidSignatures_.insert(transaction.source());
                continue;
            }

            csdb::Pool poolWithInitTr = context.blockchain().loadBlock(smartRef.sequence);
            if (!poolWithInitTr.is_valid()) {
                cslog() << kLogPrefix << "failed to load block with init transaction";
                smartSourceInvalidSignatures_.insert(transaction.source());
                continue;
            }
            //csdebug() << kLogPrefix << "Pack expired round = " << smartContractPacket.expiredRound();
            const auto& confidants = poolWithInitTr.confidants();
            const auto& signatures = smartContractPacket.signatures();
            const cs::Byte* signedHash = smartContractPacket.hash().toBinary().data();
            size_t correctSignaturesCounter = 0;
            std::string invalidSignatures;
            for (const auto& signature : signatures) {
                if (signature.first < confidants.size()) {
                    const auto& confidantPublicKey = confidants[signature.first];
                    //csdebug() << "Hash (" << static_cast<int>(signature.first) << "): " << cs::Utils::byteStreamToHex(signedHash, cscrypto::kHashSize) << " - " << smartContractPacket.hash().toString();
                    if (cscrypto::verifySignature(signature.second, confidantPublicKey, signedHash, cscrypto::kHashSize)) {
                        ++correctSignaturesCounter;
                    }
                    else {
                        invalidSignatures += (std::to_string(static_cast<int>(signature.first)) + ", ");
                    }
                }
            }
            if (correctSignaturesCounter < confidants.size() / 2U + 1U) {
                csdebug() << kLogPrefix << "is not enough valid signatures, bad ones: " << invalidSignatures << " packet hash: " << smartContractPacket.hash().toString();
                smartSourceInvalidSignatures_.insert(transaction.source());
            }
        }
    }
}

std::string IterValidator::SimpleValidator::getRejectMessage(RejectCode rc) {
    switch (rc) {
        case kAllCorrect :
            return "Transaction is correct.";
        case kInsufficientBalance :
            return "Source wallet has insufficient balance to issue trasaction.";
        case kWrongSignature :
            return "Transaction has wrong signature.";
        case kTooLarge :
            return "Transaction is too large.";
        case kInsufficientMaxFee :
            return "Transaction's max fee is not enough to issue transaction.";
        case kSourceDoesNotExists :
            return "Transaction's source doesn't exist in blockchain.";
        case kContractViolation:
            return "Contract execution violations detected";
        case kTransactionProhibited:
            return "Transaction of such type is prohibited for this account or target account";
        case kNoDelegate:
            return "No such delegate in your list";
        case kDifferentDelegatedAmount:
            return "This account has another delegation amount from your account";
        case kAmountTooLow:
            return "The amount of thansaction is too low";
        default :
            return "Unknown reject reason.";
    }
}

bool IterValidator::SimpleValidator::validate(const csdb::Transaction& t, const BlockChain& bc, SmartContracts& sc, csdb::AmountCommission* countedFeePtr, RejectCode* rcPtr) {
    RejectCode rc = kAllCorrect;

    BlockChain::WalletData wallet;
    BlockChain::WalletData tWallet;
    csdb::AmountCommission countedFee;

    if (!fee::estimateMaxFee(t, countedFee, sc)) {
        rc = kInsufficientMaxFee;
    }

    if (!rc) {
        if (cs::SmartContracts::is_executable(t) || sc.is_known_smart_contract(t.source()) || sc.is_known_smart_contract(t.target())) {
            if (sc.test_violations(t) != cs::SmartContracts::Violations::None) {
                rc = kContractViolation;
            }
        }
    }

    if (!rc && !bc.findWalletData(t.source(), wallet)) {
        rc = kSourceDoesNotExists;
    }

    csdb::UserField fld;
    fld = t.user_field(trx_uf::sp::delegated);
    bool notCheck = false;
    bool wDel = false;
    if (fld.is_valid()) {
        if (t.amount() < Consensus::MinStakeDelegated) {
            rc = kAmountTooLow;
        }
        auto flagg = fld.value<uint64_t>();
        switch(flagg) {
        case trx_uf::sp::de::legate:
                
                if (!rc) {
                    if (bc.findWalletData(t.target(), tWallet)) {
                        if (tWallet.delegated_ > csdb::Amount{ 0 }) {
                            rc = kTransactionProhibited;
                        }
                    }
                    if (sc.is_known_smart_contract(t.target())) {
                        rc = kTransactionProhibited; 
                    }
                }
                break;
        case trx_uf::sp::de::legated_withdraw:
                if (!rc) {
                    wDel = true;
                    if (bc.findWalletData(t.target(), tWallet)) {
                        auto tKey = bc.getCacheUpdater().toPublicKey(t.target());
                        auto itSource = sWallet.delegateTargets_->find(tKey);
                        if (itSource == sWallet.delegateTargets_->end()) {
                            rc = kNoDelegateTarget;//you don't contain target among your delegats
                        }
                        else {
                            auto sKey = bc.getCacheUpdater().toPublicKey(t.source());
                            auto itTarget = tWallet.delegateSources_->find(sKey);
                            if (itTarget == tWallet.delegateSources_->end()) {
                                rc = kNoDelegateSource;//target account doesn't contain you as source
                            }
                            else {
                                auto& itt = std::find_if(itTarget->second.begin(), itTarget->second.end(), [](cs::TimeMoney& tm) {return tm.time == 0U;});
                                if (itt == itTarget->second.cend()) {
                                    rc = kNoDelegatedAmountToWithdraw;//target account doesn't contain delegated amount that could be withdrawn
                                }
                                else {
                                    auto& its = std::find_if(itSource->second.begin(), itSource->second.end(), [](cs::TimeMoney& tm) {return tm.time == 0U; });
                                    if (its == itSource->second.cend()) {
                                        rc = kNoDelegatedAmountToWithdraw;//target account doesn't contain delegated amount that could be withdrawn
                                    }
                                    else {
                                        if (itt->amount != its->amount) {
                                            rc = kDifferentDelegatedAmount;//target and source have different delegate records 
                                        }
                                        else {
                                            if (itt->amount < t.amount()) {
                                                rc = kInsufficientDelegatedBalance;//previously delegated amount is not as much as you demand to withdraw
                                            }
                                            else {
                                                wDel = true;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else {
                        rc = kNoDelegate;
                    }
                }
                break;
            default:
                if (!rc) {
                    notCheck = true;
                }
                break;

        }
    }

    if (!rc && !(wallet.balance_ >= (t.amount() + t.max_fee().to_double()) || (wDel && wallet.balance_ - t.max_fee().to_double() >= csdb::Amount{ 0 }))) {
        rc = kInsufficientBalance;
    }

    if (!rc && t.to_byte_stream().size() > Consensus::MaxTransactionSize) {
        rc = kTooLarge;
    }

    if (!rc && !t.verify_signature(bc.getAddressByType(t.source(), BlockChain::AddressType::PublicKey).public_key())) {
        rc = kWrongSignature;
    }

    if (countedFeePtr) *countedFeePtr = countedFee;
    if (rcPtr) *rcPtr = rc;

    return rc == kAllCorrect;
}
}  // namespace cs
