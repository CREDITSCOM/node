#include <csnode/itervalidator.hpp>

#include <cstring>

#include <csnode/walletsstate.hpp>
#include <smartcontracts.hpp>
#include <solvercontext.hpp>

namespace {
const char* log_prefix = "Validator: ";
const uint8_t kInvalidMarker = 0;
const uint8_t kValidMarker = 1;
}  // namespace

namespace cs {

IterValidator::IterValidator(WalletsState& wallets) {
    pTransval_ = std::make_unique<TransactionsValidator>(wallets, TransactionsValidator::Config{});
}

Characteristic IterValidator::formCharacteristic(SolverContext& context, Transactions& transactions, Packets& smartsPackets) {
    cs::Characteristic characteristic;
    characteristic.mask.resize(transactions.size());
    memset(characteristic.mask.data(), kValidMarker, characteristic.mask.size());

    checkTransactionsSignatures(context, transactions, characteristic.mask, smartsPackets);

    bool needNewIteration = false;
    size_t iterationCounter = 1;
    do {
        csdebug() << log_prefix << "current iteration: " << iterationCounter;
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
    const auto& smarts = context.smart_contracts();
    std::set<csdb::Address> smart_rejected;
    size_t mask_size = characteristicMask.size();
    size_t i = 0;
    for (const auto& tr : transactions) {
        if (i < mask_size && *(characteristicMask.cbegin() + i) == kInvalidMarker) {
            if (smarts.is_known_smart_contract(tr.source())) {
                smart_rejected.insert(smarts.absolute_address(tr.source()));
            }
        }
        ++i;
    }
    if (!smart_rejected.empty()) {
        cslog() << log_prefix << "detected rejected trxs from " << smart_rejected.size() << " smart contract(s)";
        cs::TransactionsPacket rejected;
        // 2. reject all trxs from those smarts & collect all rejected trxs
        size_t cnt_add_rejected = 0;
        for (auto it = transactions.begin(); it != transactions.end(); ++it) {
            if (smart_rejected.count(smarts.absolute_address(it->source())) > 0) {
                auto itm = characteristicMask.begin() + (it - transactions.cbegin());
                if (*itm > 0) {
                    *itm = 0;
                    ++cnt_add_rejected;
                }
                rejected.addTransaction(*it);
            }
        }
        if (cnt_add_rejected > 0) {
            cslog() << log_prefix << "additionaly rejected " << cnt_add_rejected << " trxs";
        }

        // 3. signal some trxs are rejected
        if (rejected.transactionsCount() > 0) {
            std::vector<std::pair<cs::Sequence, uint32_t> > ref_list;
            for (const auto t : rejected.transactions()) {
                if (SmartContracts::is_new_state(t)) {
                    csdb::UserField fld = t.user_field(trx_uf::new_state::RefStart);
                    if (fld.is_valid()) {
                        SmartContractRef ref(fld);
                        ref_list.emplace_back(std::make_pair(ref.sequence, (uint32_t)ref.transaction));
                    }
                }
            }
            if (!ref_list.empty()) {
                context.send_rejected_smarts(ref_list);
            }
        }
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
            csdebug() << log_prefix << "transaction[" << i << "] rejected by validator";
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
        cslog() << log_prefix << "num of trxs rejected by graph validation - " << pTransval_->getCntRemovedTrxsByGraph();
        needOneMoreIteration = true;
    }

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
        cslog() << log_prefix << ": transaction[" << trxInd << "] rejected, malformed contract address";
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
                cslog() << log_prefix << "transaction[" << i << "] rejected, incorrect signature.";
                if (SmartContracts::is_new_state(transactions[i])) {
                    pTransval_->addRejectedNewState(context.smart_contracts().absolute_address(transactions[i].source()));
                }
            }
        }
    }
    if (rejectedCounter) {
        cslog() << log_prefix << "wrong signatures num: " << rejectedCounter;
    }
}

bool IterValidator::checkTransactionSignature(SolverContext& context, const csdb::Transaction& transaction) {
    BlockChain::WalletData data_to_fetch_pulic_key;
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
            context.blockchain().findWalletData(src.wallet_id(), data_to_fetch_pulic_key);
            return transaction.verify_signature(data_to_fetch_pulic_key.address_);
        }
        return transaction.verify_signature(src.public_key());
    }
    else {
        // special rule for new_state transactions
        if (SmartContracts::is_new_state(transaction) && src != transaction.target()) {
            csdebug() << log_prefix << "smart state transaction has different source and target";
            return false;
        }
        auto it = smartSourceInvalidSignatures_.find(transaction.source());
        if (it != smartSourceInvalidSignatures_.end()) {
            csdebug() << log_prefix << "smart contract transaction has invalid signature";
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
                cslog() << log_prefix << "SmartContractRef is not properly set in transaction";
                smartSourceInvalidSignatures_.insert(transaction.source());
                continue;
            }

            csdb::Pool poolWithInitTr = context.blockchain().loadBlock(smartRef.sequence);
            if (!poolWithInitTr.is_valid()) {
                cslog() << log_prefix << "failed to load block with init transaction";
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
                cslog() << log_prefix << "is not enough valid signatures";
                smartSourceInvalidSignatures_.insert(transaction.source());
            }
        }
    }
}
}  // namespace cs
