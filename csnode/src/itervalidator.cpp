#include <csnode/itervalidator.hpp>

#include <cstring>

#include <csdb/amount_commission.hpp>
#include <csnode/fee.hpp>
#include <csnode/walletsstate.hpp>
#include <smartcontracts.hpp>
#include <solvercontext.hpp>

namespace {
const char* kLogPrefix = "Validator: ";
const uint8_t kInvalidMarker = 0;
const uint8_t kValidMarker = 1;
}  // namespace

namespace cs {

IterValidator::IterValidator(WalletsState& wallets) {
    pTransval_ = std::make_unique<TransactionsValidator>(wallets, TransactionsValidator::Config{});
}

Characteristic IterValidator::formCharacteristic(SolverContext& context, Transactions& transactions, Packets& smartsPackets) {
    cs::Characteristic characteristic;
    characteristic.mask.resize(transactions.size(), kValidMarker);

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

void IterValidator::checkRejectedSmarts(SolverContext& context, cs::Bytes& characteristicMask, const Transactions& transactions) {
    // test if any of smart-emitted transaction rejected, reject all transactions from this smart
    // 1. collect rejected smart addresses
    std::vector<SolverContext::RefExecution> rejectList;
    size_t maskSize = characteristicMask.size();
    size_t i = 0;
    for (const auto& tr : transactions) {
        if (i < maskSize && *(characteristicMask.cbegin() + static_cast<std::ptrdiff_t>(i)) == kInvalidMarker) {
            if (SmartContracts::is_new_state(tr) && !pTransval_->duplicatedNewState(context, tr.source())) {
                csdb::UserField fld = tr.user_field(trx_uf::new_state::RefStart);
                if (fld.is_valid()) {
                    SmartContractRef ref(fld);
                    rejectList.emplace_back(std::make_pair(ref.sequence, static_cast<uint32_t>(ref.transaction)));
                }
            }
        }
        ++i;
    }
    if (!rejectList.empty()) {
        cslog() << kLogPrefix << "reject " << rejectList.size() << " new_state(s) of smart contract(s)";
        context.send_rejected_smarts(rejectList);
    }
}

bool IterValidator::validateTransactions(SolverContext& context, cs::Bytes& characteristicMask, const Transactions& transactions) {
    bool needOneMoreIteration = false;
    const size_t transactionsCount = transactions.size();
    size_t blockedCounter = 0;

    // validate each transaction
    for (size_t i = 0; i < transactionsCount; ++i) {
        if (characteristicMask[i] == kInvalidMarker) {
            continue;
        }

        const csdb::Transaction& transaction = transactions[i];
        bool isValid = pTransval_->validateTransaction(context, transactions, i);

        if (isValid && SmartContracts::is_deploy(transaction)) {
            isValid = deployAdditionalCheck(context, i, transaction);
        }

        if (!isValid) {
            csdebug() << kLogPrefix << "transaction[" << i << "] rejected by validator";
            characteristicMask[i] = kInvalidMarker;
            needOneMoreIteration = true;
            ++blockedCounter;
        }
        else {
            characteristicMask[i] = kValidMarker;
        }
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

bool IterValidator::deployAdditionalCheck(SolverContext& context, size_t trxInd, const csdb::Transaction& transaction) {
    // test with get_valid_smart_address() only for deploy transactions
    bool isValid = true;
    auto sci = context.smart_contracts().get_smart_contract(transaction);

    if (sci.has_value() && sci.value().method.empty()) {  // is deploy
        csdb::Address deployer = context.blockchain().getAddressByType(transaction.source(), BlockChain::AddressType::PublicKey);
        isValid = SmartContracts::get_valid_smart_address(deployer, transaction.innerID(), sci.value().smartContractDeploy) == transaction.target();
    }

    if (!isValid) {
        cslog() << kLogPrefix << ": transaction[" << trxInd << "] rejected, malformed contract address";
    }

    return isValid;
}

void IterValidator::checkTransactionsSignatures(SolverContext& context, const Transactions& transactions, cs::Bytes& characteristicMask, Packets& smartsPackets) {
    checkSignaturesSmartSource(context, smartsPackets);
    size_t transactionsCount = transactions.size();
    size_t maskSize = characteristicMask.size();
    size_t rejectedCounter = 0;
    for (size_t i = 0; i < transactionsCount; ++i) {
        if (i < maskSize) {
            bool correctSignature = checkTransactionSignature(context, transactions[i]);
            if (!correctSignature) {
                characteristicMask[i] = kInvalidMarker;
                rejectedCounter++;
                cslog() << kLogPrefix << "transaction[" << i << "] rejected, incorrect signature.";
                if (SmartContracts::is_new_state(transactions[i])) {
                    pTransval_->addRejectedNewState(context.smart_contracts().absolute_address(transactions[i].source()));
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

void IterValidator::checkSignaturesSmartSource(SolverContext& context, cs::Packets& smartContractsPackets) {
    smartSourceInvalidSignatures_.clear();

    for (auto& smartContractPacket : smartContractsPackets) {
        if (smartContractPacket.transactions().size() > 0) {
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

            const auto& confidants = poolWithInitTr.confidants();
            const auto& signatures = smartContractPacket.signatures();
            size_t correctSignaturesCounter = 0;
            for (const auto& signature : signatures) {
                if (signature.first < confidants.size()) {
                    const auto& confidantPublicKey = confidants[signature.first];
                    const cs::Byte* signedHash = smartContractPacket.hash().toBinary().data();
                    if (cscrypto::verifySignature(signature.second, confidantPublicKey, signedHash, cscrypto::kHashSize)) {
                        ++correctSignaturesCounter;
                    }
                }
            }
            if (correctSignaturesCounter < confidants.size() / 2U + 1U) {
                cslog() << kLogPrefix << "is not enough valid signatures";
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
        case kInsufficientMaxFee :
            return "Transaction's max fee is not enough to issue transaction.";
        case kSourceDoesNotExists :
            return "Transaction's source doesn't exist in blockchain.";
        default :
            return "Unknown reject reason.";
    }
}

bool IterValidator::SimpleValidator::validate(const csdb::Transaction& t, const BlockChain& bc, csdb::AmountCommission* countedFeePtr, RejectCode* rcPtr) {
    RejectCode rc = kAllCorrect;

    BlockChain::WalletData wallet;
    csdb::AmountCommission countedFee;

    if (!fee::estimateMaxFee(t, countedFee)) {
        rc = kInsufficientMaxFee;
    }

    if (!rc && !bc.findWalletData(t.source(), wallet)) {
        rc = kSourceDoesNotExists;
    }

    if (!rc && wallet.balance_ < (t.amount() + t.max_fee().to_double())) {
        rc = kInsufficientBalance;
    }

    if (!rc && !t.verify_signature(bc.getAddressByType(t.source(), BlockChain::AddressType::PublicKey).public_key())) {
        rc = kWrongSignature;
    }

    if (countedFeePtr) *countedFeePtr = countedFee;
    if (rcPtr) *rcPtr = rc;

    return rc == kAllCorrect;
}
}  // namespace cs
