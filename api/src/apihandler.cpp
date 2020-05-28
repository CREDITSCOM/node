#include <apihandler.hpp>

#include <csnode/fee.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/itervalidator.hpp>
#include <csnode/transactionsiterator.hpp>
#include <csnode/configholder.hpp>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <solver/smartcontracts.hpp>

#include <src/priv_crypto.hpp>

#include "csconnector/csconnector.hpp"

#include "stdafx.h"
#include "serializer.hpp"

#include <base58.h>

using namespace api;
using namespace ::apache;

inline int64_t limitPage(int64_t value) {
    return std::clamp(value, int64_t(0), int64_t(100));
}

inline int64_t limitQueries(int64_t value, int64_t maxQueries) {
    return std::clamp(value, int64_t(0), int64_t(maxQueries));
}

apiexec::APIEXECHandler::APIEXECHandler(BlockChain& blockchain, cs::SolverCore& solver, cs::Executor& executor)
: executor_(executor)
, blockchain_(blockchain)
, solver_(solver) {
}

APIHandler::APIHandler(BlockChain& blockchain, cs::SolverCore& _solver, cs::Executor& executor)
: executor_(executor)
, blockchain_(blockchain)
, solver_(_solver)
#ifdef USE_DEPRECATED_STATS //MONITOR_NODE
, stats(blockchain)
#endif
, tm_(this) {
#ifdef USE_DEPRECATED_STATS //MONITOR_NODE
    if (static bool firstTime = true; firstTime) {
        stats_.second.resize(::csstats::collectionPeriods.size());
        auto nowGlobal = std::chrono::system_clock::now();
        auto lastTimePoint = nowGlobal - std::chrono::seconds(::csstats::collectionPeriods[::csstats::PeriodIndex::Month]);

        for (auto time = nowGlobal; time > lastTimePoint; time -= std::chrono::seconds(::csstats::updateTimeSec)) {
            ::csstats::PeriodStats cut;
            cut.timeStamp = time;
            stats_.first.push_back(cut);
        }
        firstTime = false;
    }
#endif
}

void APIHandler::run() {
    if (!blockchain_.isGood())
        return;
#ifdef USE_DEPRECATED_STATS //MONITOR_NODE
    stats.run(stats_);
#endif
    state_updater_running.test_and_set(std::memory_order_acquire);
}

APIHandler::~APIHandler() {
    state_updater_running.clear(std::memory_order_release);

    if (state_updater.joinable()) {
        state_updater.join();
    }
}

template <typename ResultType>
bool validatePagination(ResultType& _return, APIHandler& handler, int64_t offset, int64_t limit) {
    if (offset < 0 || limit <= 0 || limit > 100) {
        handler.SetResponseStatus(_return.status, APIHandlerBase::APIRequestStatusType::FAILURE);
        return false;
    }

    return true;
}

void APIHandlerBase::SetResponseStatus(general::APIResponse& response, APIRequestStatusType status, const std::string& details) {
    struct APIRequestStatus {
        APIRequestStatus(uint8_t code, std::string message)
        : message(message)
        , code(code) {
        }
        std::string message;
        uint8_t code;
    };

    APIRequestStatus statuses[static_cast<size_t>(APIHandlerBase::APIRequestStatusType::MAX)] = {
        {0, "Success"},
        {1, "Failure"},
        {2, "Not Implemented"},
        {3, "Not found"},
        {4, "Transaction in progress"}
    };

    response.code = int8_t(statuses[static_cast<uint8_t>(status)].code);
    response.message = statuses[static_cast<uint8_t>(status)].message + ": " + details;
}

void APIHandlerBase::SetResponseStatus(general::APIResponse& response, bool commandWasHandled) {
    SetResponseStatus(response, (commandWasHandled ? APIRequestStatusType::SUCCESS : APIRequestStatusType::NOT_IMPLEMENTED));
}

void APIHandler::WalletDataGet(WalletDataGetResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::WalletData wallData{};
    BlockChain::WalletId wallId{};
    if (!blockchain_.findWalletData(addr, wallData, wallId)) {
        if (!blockchain_.findWalletData(addr, wallData)) { // **
            return;
        }
    }
    _return.walletData.walletId = static_cast<int>(wallId); // may be default value if **
    _return.walletData.balance.integral = wallData.balance_.integral();
    _return.walletData.balance.fraction = static_cast<decltype(_return.walletData.balance.fraction)>(wallData.balance_.fraction());
    const cs::TransactionsTail& tail = wallData.trxTail_;
    _return.walletData.lastTransactionId = tail.empty() ? 0 : tail.getLastTransactionId();

    std::optional delegated = getDelegated(wallData);
    if (delegated.has_value()) {
        _return.walletData.__set_delegated(delegated.value());
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::WalletIdGet(api::WalletIdGetResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::WalletData wallData{};
    BlockChain::WalletId wallId{};
    if (!blockchain_.findWalletData(addr, wallData, wallId)) {
        SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
        return;
    }

    _return.walletId = static_cast<int>(wallId);
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::WalletTransactionsCountGet(api::WalletTransactionsCountGetResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::WalletData wallData{};
    if (!blockchain_.findWalletData(addr, wallData)) {
        SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
        return;
    }
    _return.lastTransactionInnerId = wallData.trxTail_.empty() ? 0 : wallData.trxTail_.getLastTransactionId();
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::WalletBalanceGet(api::WalletBalanceGetResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::WalletData wallData{};
    if (!blockchain_.findWalletData(addr, wallData)) {
        return;
    }
    _return.balance.integral = wallData.balance_.integral();
    _return.balance.fraction = static_cast<decltype(_return.balance.fraction)>(wallData.balance_.fraction());
    std::optional<api::Delegated> delegated = getDelegated(wallData);
    if (delegated.has_value()) {
        _return.__set_delegated(delegated.value());
    }
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

std::string fromByteArray(const cs::Bytes& bar) {
    return std::string(bar.begin(), bar.end());
}

std::string fromByteArray(const cs::PublicKey& bar) {
    return std::string(bar.begin(), bar.end());
}

cs::Bytes toByteArray(const std::string& s) {
    return cs::Bytes(s.begin(), s.end());
}

general::Amount convertAmount(const csdb::Amount& amount) {
    general::Amount result;
    result.integral = amount.integral();
    result.fraction = static_cast<int64_t>(amount.fraction());
    assert(result.fraction >= 0);
    return result;
}

api::TransactionId convert_transaction_id(const csdb::TransactionID& trid) {
    api::TransactionId result_id;
    result_id.index = int32_t(trid.index());
    result_id.poolSeq = static_cast<int64_t>(trid.pool_seq());
    result_id.__isset.index = true;
    result_id.__isset.poolSeq = true;
    return result_id;
}

csdb::TransactionID convert_transaction_id(const api::TransactionId& trid) {
    return csdb::TransactionID(cs::Sequence(trid.poolSeq), cs::Sequence(trid.index));
}

bool is_smart(const csdb::Transaction& tr) {
    using namespace cs::trx_uf;
    // deploy::Code == start::Methods == 0
    return tr.user_field(deploy::Code).type() == csdb::UserField::Type::String;
}

bool is_smart_state(const csdb::Transaction& tr) {
    using namespace cs::trx_uf;
    // new_state may contain state value or state hash, both variants are valid
    // test user_field[RefStart] helps filter out ancient smart contracts:
    bool has_state = tr.user_field(new_state::Value).type() == csdb::UserField::Type::String ||
        tr.user_field(new_state::Hash).type() == csdb::UserField::Type::String;
    return (has_state && tr.user_field(new_state::RefStart).type() == csdb::UserField::Type::String);
}

bool is_smart_deploy(const api::SmartContractInvocation& smart) {
    return smart.method.empty();
}

bool is_deploy_transaction(const csdb::Transaction& tr) {
    using namespace cs::trx_uf;
    auto uf = tr.user_field(deploy::Code);
    return uf.type() == csdb::UserField::Type::String && is_smart_deploy(cs::Serializer::deserialize<api::SmartContractInvocation>(uf.value<std::string>()));
}

std::optional<api::Delegated> APIHandler::getDelegated(const BlockChain::WalletData& wallet) {
    csdb::Amount zero(0);
    api::Delegated delegated;
    bool has_delegations = false;

    // fill incoming delegations total sum
    if (wallet.delegated_ > zero) {
        auto tmp = convertAmount(wallet.delegated_);
        delegated.__set_incoming(tmp);
        has_delegations = true;
    }

    // fill donors list
    if (wallet.delegateSources_ && !wallet.delegateSources_->empty()) {
        has_delegations = true;
        std::vector<api::DelegatedItem> donors;
        for (const auto& item : *wallet.delegateSources_) {

            for (const auto& tm : item.second) {
                api::DelegatedItem donor;
                donor.__set_wallet(fromByteArray(item.first));
                auto s = convertAmount(tm.amount);
                donor.__set_sum(s);
                if (tm.time > 0) {
                    donor.__set_validUntil(tm.time);
                }
                donors.push_back(donor);
            }
        }
        if (!donors.empty()) {
            delegated.__set_donors(donors);
        }
    }

    // fill recipients list after it is implemented, calculate total sum on-the-fly
    if (wallet.delegateTargets_ && !wallet.delegateTargets_->empty()) {
        has_delegations = true;
        csdb::Amount sum(0);
        std::vector<api::DelegatedItem> recipients;
        for (const auto& item : *wallet.delegateTargets_) {

            for (const auto& tm : item.second) {
                if (tm.amount > zero) {
                    sum += tm.amount;
                }
                api::DelegatedItem recipient;
                recipient.__set_wallet(fromByteArray(item.first));
                auto s = convertAmount(tm.amount);
                recipient.__set_sum(s);
                if (tm.time > 0) {
                    recipient.__set_validUntil(tm.time);
                }
                recipients.push_back(recipient);
            }

        }
        if (sum > zero) {
            auto am = convertAmount(sum);
            delegated.__set_outgoing(am);
        }
        if (!recipients.empty()) {
            delegated.__set_recipients(recipients);
        }
    }

    if (has_delegations || delegated.__isset.donors || delegated.__isset.recipients) {
        return std::make_optional(std::move(delegated));
    }
    return std::nullopt;
}

APIHandler::SmartOperation APIHandler::getSmartStatus(const csdb::TransactionID tId) {
    auto sop = lockedReference(smart_operations);
    auto it = sop->find(tId);
    if (it == sop->end())
        return SmartOperation();
    return it->second;
}

template <typename SmartOp, typename TransInfo>
static void fillTransInfoWithOpData(const SmartOp& op, TransInfo& ti) {
    ti.state = api::SmartOperationState(op.state);
    if (op.stateTransaction.is_valid())
        ti.__set_stateTransaction(convert_transaction_id(op.stateTransaction));
}

api::SealedTransaction APIHandler::convertTransaction(const csdb::Transaction& transaction) {
    using namespace cs::trx_uf;
    api::SealedTransaction result;
    const csdb::Amount amount = transaction.amount();
    csdb::Currency currency = transaction.currency();
    csdb::Address address = transaction.source();

    if (address.is_wallet_id()) {
        address = blockchain_.getAddressByType(transaction.source(), BlockChain::AddressType::PublicKey);
    }

    csdb::Address target = transaction.target();

    if (target.is_wallet_id()) {
        target = blockchain_.getAddressByType(transaction.target(), BlockChain::AddressType::PublicKey);
    }

    result.id = convert_transaction_id(transaction.id());
    result.__isset.id = true;
    result.__isset.trxn = true;
    result.trxn.id = transaction.innerID();
    result.trxn.amount = convertAmount(amount);
    result.trxn.currency = DEFAULT_CURRENCY;
    result.trxn.source = fromByteArray(address.public_key());
    result.trxn.target = fromByteArray(target.public_key());
    result.trxn.fee.commission = int16_t(transaction.counted_fee().get_raw());
    result.trxn.timeCreation = static_cast<int64_t>(transaction.get_time());
    result.trxn.poolNumber = static_cast<int64_t>(transaction.id().pool_seq());

    bool is_contract_replenish = false;

    if (is_smart(transaction)) {
        auto sci = cs::Serializer::deserialize<api::SmartContractInvocation>(transaction.user_field(deploy::Code).value<std::string>());
        bool isToken = false;

        auto smartResult = getSmartStatus(transaction.id());
        result.trxn.__set_smartInfo(api::SmartTransInfo{});

        if (is_smart_deploy(sci)) {
            result.trxn.type = api::TransactionType::TT_ContractDeploy;
            tm_.loadTokenInfo([&isToken, &target, &result](const TokensMap& tokens, const HoldersMap&) {
                auto it = tokens.find(target);
                if (it != tokens.end()) {
                    isToken = true;
                    api::TokenDeployTransInfo dti;
                    dti.name = it->second.name;
                    dti.code = it->second.symbol;
                    dti.tokenStandard = int32_t(it->second.tokenStandard);
                    result.trxn.smartInfo.__set_v_tokenDeploy(dti);
                    result.trxn.type = api::TransactionType::TT_TokenDeploy;
                }
            });

            if (isToken)
                fillTransInfoWithOpData(smartResult, result.trxn.smartInfo.v_tokenDeploy);
            else {
                result.trxn.smartInfo.__set_v_smartDeploy(SmartDeployTransInfo());
                fillTransInfoWithOpData(smartResult, result.trxn.smartInfo.v_smartDeploy);
            }
        }
        else {
            bool isTransfer = TokensMaster::isTransfer(sci.method, sci.params);
            result.trxn.type = api::TransactionType::TT_ContractCall;
            if (isTransfer) {
                tm_.loadTokenInfo([&isToken, &isTransfer, &target, &result](const TokensMap& tokens, const HoldersMap&) {
                    auto it = tokens.find(target);
                    if (it != tokens.end()) {
                        isToken = true;
                        api::TokenTransferTransInfo tti;
                        tti.code = it->second.symbol;
                        result.trxn.smartInfo.__set_v_tokenTransfer(tti);
                        result.trxn.type = api::TransactionType::TT_TokenTransfer;
                    }
                    else
                        isTransfer = false;
                });
            }

            if (isTransfer) {
                auto addrPair = TokensMaster::getTransferData(address, sci.method, sci.params);

                result.trxn.smartInfo.v_tokenTransfer.sender = fromByteArray(addrPair.first.public_key());
                result.trxn.smartInfo.v_tokenTransfer.receiver = fromByteArray(addrPair.second.public_key());
                result.trxn.smartInfo.v_tokenTransfer.amount = TokensMaster::getAmount(sci);

                if (smartResult.hasReturnValue())
                    result.trxn.smartInfo.v_tokenTransfer.__set_transferSuccess(smartResult.getReturnedBool());

                fillTransInfoWithOpData(smartResult, result.trxn.smartInfo.v_tokenTransfer);
            }
            else {
                SmartExecutionTransInfo eti;
                eti.method = sci.method;
                eti.params = sci.params;
                fillTransInfoWithOpData(smartResult, eti);

                result.trxn.smartInfo.__set_v_smartExecution(eti);
            }
        }

        result.trxn.__set_smartContract(sci);
    }
    else if (is_smart_state(transaction)) {
        result.trxn.type = api::TransactionType::TT_ContractState;
        api::SmartStateTransInfo sti;
        sti.success = cs::SmartContracts::is_state_updated(transaction);
        sti.executionFee = convertAmount(transaction.user_field(new_state::Fee).value<csdb::Amount>());
        cs::SmartContractRef scr;
        scr.from_user_field(transaction.user_field(new_state::RefStart));
        sti.startTransaction = convert_transaction_id(scr.getTransactionID());

        auto fld = transaction.user_field(new_state::RetVal);
        if (fld.is_valid()) {
            auto retVal = fld.value<std::string>();
            auto variant = cs::Serializer::deserialize<::general::Variant>(std::move(retVal));
            // override retValue with text message if new state is empty
            if (sti.success) {
                sti.__set_returnValue(variant);
            }
            else {
                if (variant.__isset.v_byte) {
                    // if not success and variant is of byte type there is an error code
                    variant.__set_v_string(cs::SmartContracts::get_error_message(variant.v_byte));
                }
                sti.__set_returnValue(variant);
            }
        }
        result.trxn.smartInfo.__set_v_smartState(sti);
        result.trxn.__isset.smartInfo = true;
    }
    else {
        result.trxn.type = api::TransactionType::TT_Transfer;

        if (solver_.smart_contracts().is_payable_call(transaction)) {
            is_contract_replenish = true;
            result.trxn.type = api::TransactionType::TT_ContractReplenish;
            auto smartResult = getSmartStatus(transaction.id());
            SmartExecutionTransInfo eti;
            eti.method = "payable";
            general::Variant param;
            general::Amount sum;
            sum.__set_integral(transaction.amount().integral());
            sum.__set_fraction(transaction.amount().fraction());
            param.__set_v_amount(sum);
            std::vector<general::Variant> params;
            params.push_back(param);
            eti.__set_params(params);
            fillTransInfoWithOpData(smartResult, eti);
            api::SmartTransInfo info;
            info.__set_v_smartExecution(eti);
            result.trxn.__set_smartInfo(info);
        }
        else if (solver_.smart_contracts().is_known_smart_contract(transaction.source())) {
            result.trxn.type = api::TransactionType::TT_ContractEmitted;
            // move backwards through trxs until new state has found
            const auto seq = transaction.id().pool_seq();
            for (auto idx = transaction.id().index() - 1; idx >= 0; --idx) {
                const csdb::TransactionID id(seq, idx);
                csdb::Transaction t = executor_.loadTransactionApi(id);
                if (is_smart_state(t)) {
                    csdb::UserField fld = t.user_field(cs::trx_uf::new_state::RefStart);
                    if (fld.is_valid()) {
                        cs::SmartContractRef ref(fld);
                        api::SmartStateTransInfo sti;
                        sti.startTransaction = convert_transaction_id(ref.getTransactionID());
                        api::SmartTransInfo info;
                        info.__set_v_smartState(sti);
                        result.trxn.__set_smartInfo(info);
                    }
                    break;
                }
            }
        }

        auto ufd = transaction.user_field(ordinary::Text);
        if (ufd.is_valid()) {
            result.trxn.__set_userFields(ufd.value<std::string>());
        }
        auto ufdDel = transaction.user_field(sp::delegated);
        if (ufdDel.is_valid()) {
            cs::Bytes msg;
            cs::ODataStream stream(msg);
            //Aufmerksamkeit!!! Bemerken, bitte, dass dieser Codeteil nur fuer die Einuserfieldstransactionen leistungsfaehig ist.
            stream << uint8_t(0U); //different uf flagg
            stream << uint8_t(1U); //uf number
            stream << uint32_t(5U); //uf ID
            stream << uint8_t(1U); //type = int
            const uint64_t opcode = ufdDel.value<uint64_t>();
            stream << opcode; // value
            std::string tmp((char*)(msg.data()), msg.size());
            result.trxn.__set_userFields(tmp);
            if (opcode == sp::de::legate || opcode >= sp::de::legate_min_utc) {
                result.trxn.type = api::TransactionType::TT_Delegation;
            }
            else {
                switch (opcode) {
                case sp::de::legated_withdraw:
                    result.trxn.type = api::TransactionType::TT_RevokeDelegation;
                    break;
                case sp::de::legated_release:
                    result.trxn.type = api::TransactionType::TT_Release;
                    break;
                default:
                    // actually unreachable
                    break;
                }
            }
        }
    }

    // fill ExtraFee
    // 1) find state transaction
    csdb::Transaction stateTrx;
    if (is_smart(transaction) || is_contract_replenish) {
        auto opers = lockedReference(this->smart_operations);
        auto state_id = (*opers)[transaction.id()].stateTransaction;
        if (state_id.is_valid()) {
            stateTrx = executor_.loadTransactionApi(state_id);
        }
    }
    else if (is_smart_state(transaction)) {
        stateTrx = transaction;
    }

    if (!is_smart_state(stateTrx))
        return result;

    // 2) fill ExtraFee for state transaction
    auto pool = executor_.loadBlockApi(stateTrx.id().pool_seq());
    auto transactions = pool.transactions();
    ExtraFee extraFee;
    extraFee.transactionId = convert_transaction_id(stateTrx.id());
    // 2.1) counted_fee 
    extraFee.sum = convertAmount(csdb::Amount(stateTrx.counted_fee().to_double()));
    extraFee.comment = "contract state fee";
    result.trxn.extraFee.push_back(extraFee);
    // 2.2) execution fee
    extraFee.sum = convertAmount(stateTrx.user_field(cs::trx_uf::new_state::Fee).value<csdb::Amount>());
    extraFee.comment = "contract execution fee";
    result.trxn.extraFee.push_back(extraFee);

    // 3) fill ExtraFee for extra transactions
    auto trxIt = std::find_if(transactions.begin(), transactions.end(), [&stateTrx](const csdb::Transaction& ptrx) { return ptrx.id() == stateTrx.id(); });
    if (trxIt != transactions.end()) {
        for (auto trx = ++trxIt; trx != transactions.end(); ++trx) {
            if (blockchain_.getAddressByType(trx->source(), BlockChain::AddressType::PublicKey) !=
                blockchain_.getAddressByType(stateTrx.source(), BlockChain::AddressType::PublicKey)) // end find extra transactions
                break;
            extraFee.transactionId = convert_transaction_id(trx->id());
            extraFee.sum = convertAmount(csdb::Amount(trx->counted_fee().to_double()));
            extraFee.comment = "emitted trxs fee";
            result.trxn.extraFee.push_back(extraFee);
        }
    }
    result.trxn.__isset.extraFee = true;
    return result;
}

std::vector<api::SealedTransaction> APIHandler::convertTransactions(const std::vector<csdb::Transaction>& transactions) {
    std::vector<api::SealedTransaction> result;
    auto size = transactions.size();
    result.resize(size);

    for (size_t Count = 0; Count < result.size(); Count++) {
        result[Count] = convertTransaction(transactions[Count]);
    }
    return result;
}

api::Pool APIHandler::convertPool(const csdb::Pool& pool) {
    api::Pool result;
    pool.is_valid();

    if (pool.is_valid()) {
        result.hash = fromByteArray(pool.hash().to_binary());
        result.poolNumber = static_cast<int64_t>(pool.sequence());
        assert(result.poolNumber >= 0);
        result.prevHash = fromByteArray(pool.previous_hash().to_binary());
        result.time = static_cast<int64_t>(BlockChain::getBlockTime(pool));

        result.transactionsCount = int32_t(pool.transactions_count());  // DO NOT EVER CREATE POOLS WITH
                                                                        // MORE THAN 2 BILLION
                                                                        // TRANSACTIONS, EVEN AT NIGHT

        const auto& wpk = pool.writer_public_key();
        result.writer = fromByteArray(cs::Bytes(wpk.begin(), wpk.end()));

        double totalFee = 0;
        const auto& transs = const_cast<csdb::Pool&>(pool).transactions();
        for (auto& t : transs) {
            totalFee += t.counted_fee().to_double();
        }

        const auto tf = csdb::Amount(totalFee);
        result.totalFee.integral = tf.integral();
        result.totalFee.fraction = static_cast<int64_t>(tf.fraction());
    }

    return result;
}

api::Pool APIHandler::convertPool(const csdb::PoolHash& poolHash) {
    return convertPool(executor_.loadBlockApi(poolHash));
}

std::vector<api::SealedTransaction> APIHandler::extractTransactions(const csdb::Pool& pool, int64_t limit, const int64_t offset) {
    int64_t transactionsCount = static_cast<int64_t>(pool.transactions_count());
    assert(transactionsCount >= 0);
    std::vector<api::SealedTransaction> result;

    if (offset > transactionsCount) {
        return result;  // если запрашиваемые // транзакций выходят за // пределы пула возвращаем пустой результат
    }

    transactionsCount -= offset;  // мы можем отдать все транзакции в пуле за вычетом смещения

    if (limit > transactionsCount) {
        limit = transactionsCount;  // лимит уменьшается до реального количества // транзакций которые можно отдать
    }

    for (int64_t index = offset; index < (offset + limit); ++index) {
        auto tr = pool.transaction(static_cast<size_t>(index));
        tr.set_time(BlockChain::getBlockTime(pool));
        result.push_back(convertTransaction(tr));
    }

    return result;
}

void APIHandler::TransactionGet(TransactionGetResult& _return, const TransactionId& transactionId) {
    const csdb::TransactionID tmpTransactionId = csdb::TransactionID(cs::Sequence(transactionId.poolSeq), cs::Sequence(transactionId.index));
    csdb::Transaction transaction = executor_.loadTransactionApi(tmpTransactionId);
    _return.found = transaction.is_valid();
    if (_return.found)
        _return.transaction = convertTransaction(transaction);

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS, std::to_string(transaction.counted_fee().to_double()));
}

void APIHandler::TransactionsGet(TransactionsGetResult& _return, const general::Address& address, const int64_t _offset, const int64_t const_limit) {
    auto limit = limitPage(const_limit);
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::Transactions transactions;

    if (limit > 0) {
        const int64_t offset = (_offset < 0) ? 0 : _offset;
        blockchain_.getTransactions(transactions, addr, static_cast<uint64_t>(offset), static_cast<uint64_t>(limit));
    }

    _return.transactions = convertTransactions(transactions);
    _return.total_trxns_count = static_cast<int32_t>(blockchain_.getTransactionsCount(addr));
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

api::SmartContractInvocation fetch_smart(const csdb::Transaction& tr) {
    if (tr.is_valid()) {
        const auto uf = tr.user_field(cs::trx_uf::deploy::Code);
        if (uf.is_valid()) {
            std::string data = uf.value<std::string>();
            if (!data.empty()) {
                return cs::Serializer::deserialize<api::SmartContractInvocation>(std::move(data));
            }
        }
    }
    return api::SmartContractInvocation();
}

api::SmartContract APIHandler::fetch_smart_body(const csdb::Transaction& tr) {
    using namespace cs::trx_uf;
    api::SmartContract res;

    if (!tr.is_valid()) {
        return res;
    }

    const auto sci = cs::Serializer::deserialize<api::SmartContractInvocation>(tr.user_field(deploy::Code).value<std::string>());
    res.smartContractDeploy.byteCodeObjects = sci.smartContractDeploy.byteCodeObjects;
    res.smartContractDeploy.sourceCode = sci.smartContractDeploy.sourceCode;
    res.smartContractDeploy.hashState = sci.smartContractDeploy.hashState;
    res.deployer = fromByteArray(blockchain_.getAddressByType(tr.source(), BlockChain::AddressType::PublicKey).public_key());
    res.address = fromByteArray(blockchain_.getAddressByType(tr.target(), BlockChain::AddressType::PublicKey).public_key());

#ifdef TOKENS_CACHE
    tm_.loadTokenInfo([&tr, &res](const TokensMap& tokens, const HoldersMap&) {
        auto it = tokens.find(tr.target());
        if (it != tokens.end()) {
            res.smartContractDeploy.tokenStandard = int32_t(it->second.tokenStandard);
        }
        else {
            res.smartContractDeploy.tokenStandard = int32_t(TokenStandard::NotAToken);
        }
    });
#else
    res.smartContractDeploy.tokenStandard = TokenStandard::NotAToken;
#endif

#ifdef MONITOR_NODE
    blockchain_.applyToWallet(tr.target(), [&res](const cs::WalletsCache::WalletData& wd) { res.createTime = wd.createTime_; });
#endif
    if (tr.user_field(0).is_valid()) {
        res.transactionsCount = static_cast<int32_t>(blockchain_.getTransactionsCount(tr.target()));
    }

    auto pool = executor_.loadBlockApi(tr.id().pool_seq());
    res.createTime = static_cast<int64_t>(BlockChain::getBlockTime(pool));

    return res;
}

template <typename T>
auto set_max_fee(T& trx, const csdb::Amount& am, int) -> decltype(trx.set_max_fee(am), void()) {
    trx.set_max_fee(am);
}

template <typename T>
void set_max_fee(T&, const csdb::Amount&, long) {
}

csdb::Transaction APIHandler::makeTransaction(const Transaction& transaction) {
    csdb::Transaction send_transaction;
    const auto source = BlockChain::getAddressFromKey(transaction.source);
    const uint64_t WALLET_DENOM = csdb::Amount::AMOUNT_MAX_FRACTION;  // 1'000'000'000'000'000'000ull;
    send_transaction.set_amount(csdb::Amount(transaction.amount.integral, static_cast<uint64_t>(transaction.amount.fraction), WALLET_DENOM));
    BlockChain::WalletData dummy{};

    if (transaction.__isset.smartContract && !transaction.smartContract.forgetNewState &&  // not for getter
            !blockchain_.findWalletData(source, dummy)) {
        return csdb::Transaction{};
    }

    send_transaction.set_currency(csdb::Currency(1));
    send_transaction.set_source(source);
    send_transaction.set_target(BlockChain::getAddressFromKey(transaction.target));
    send_transaction.set_max_fee(csdb::AmountCommission(uint16_t(transaction.fee.commission)));
    send_transaction.set_innerID(transaction.id & 0x3fffffffffff);

    // TODO Change Thrift to avoid copy
    cs::Signature signature{};

    if (transaction.signature.size() == signature.size()) {
        std::copy(transaction.signature.begin(), transaction.signature.end(), signature.begin());
    }

    send_transaction.set_signature(signature);
    return send_transaction;
}

api::ShortTransaction APIHandler::convertTransactionToShort(csdb::Transaction tr) {
    api::ShortTransaction apiTr;
    auto sTr = convertTransaction(tr);
    apiTr.amount = sTr.trxn.amount;
    apiTr.currency = sTr.trxn.currency;
    apiTr.fee = sTr.trxn.fee;
    apiTr.id = sTr.id;
    apiTr.source = sTr.trxn.source;
    apiTr.target = sTr.trxn.target;
    apiTr.timeCreation = sTr.trxn.timeCreation;
    apiTr.type = sTr.trxn.type;
    //auto extraFee = sTr.trxn.extraFee;
    //auto executeResult = sTr.trxn;
    return apiTr;
}

std::string getDelimitedTransactionSigHex(const csdb::Transaction& tr) {
    auto bs = fromByteArray(tr.to_byte_stream_for_sig());
    return std::string({' '}) + cs::Utils::byteStreamToHex(bs.data(), bs.length());
}

void APIHandler::dumbTransactionFlow(api::TransactionFlowResult& _return, const csdb::Transaction& tr) {
    //auto tr = makeTransaction(transaction);

    // remember dumb transaction 
    if (!dumbCv_.addCVInfo(tr.signature())) {
        _return.status.code = int8_t(ERROR_CODE);
        _return.status.message = "This transaction has been seen before";
        return;
    }

    cs::Conveyer::instance().addTransaction(tr);

    // wait for transaction in blockchain
    auto result = dumbCv_.waitCvSignal(tr.signature());

    switch (result.condition) {
    case cs::DumbCv::Condition::Success: {
            auto newTransactionId = result.id;
            _return.id.poolSeq = static_cast<int64_t>(newTransactionId.pool_seq());
            _return.id.index = static_cast<int32_t>(newTransactionId.index());
            auto countedFee = result.fee;
            _return.fee.fraction = countedFee.fraction();
            _return.fee.integral = countedFee.integral();
            SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS, getDelimitedTransactionSigHex(tr));
        }
        break;

    case cs::DumbCv::Condition::Expired:
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE, "transaction is expired");
        break;

    case cs::DumbCv::Condition::Rejected:
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE, "transaction is rejected by consensus");
        break;

    case cs::DumbCv::Condition::TimeOut:
        SetResponseStatus(_return.status, APIRequestStatusType::INPROGRESS);
        break;

    default:
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE, "wrong Node behaviour");
    }
}

template <typename T>
std::enable_if<std::is_convertible<T*, ::apache::thrift::TBase*>::type, std::ostream&> operator<<(std::ostream& s, const T& t) {
    t.printTo(s);
    return s;
}

std::optional<std::string> APIHandler::checkTransaction(const Transaction& transaction, csdb::Transaction& cTransaction) {
    cTransaction = makeTransaction(transaction);

    if (transaction.__isset.smartContract && transaction.smartContract.forgetNewState) {
        cTransaction.add_user_field(cs::trx_uf::deploy::Code, cs::Serializer::serialize(transaction.smartContract));
        return std::nullopt;
    }

    if (transaction.__isset.smartContract) {
        if (transaction.smartContract.__isset.smartContractDeploy) {
            // deploy info provided
            if (!transaction.smartContract.method.empty()) {
                // call to contract, must not be with deploy info
                return std::make_optional("Malformed contract execution, unnecessary info provided (smartContractDeploy)");
            }
            if (transaction.smartContract.smartContractDeploy.sourceCode.empty()) {
                // source code is absent
                return std::make_optional("Malformed contract deployment, source code is not provided");
            }
        }
        cTransaction.add_user_field(cs::trx_uf::deploy::Code, cs::Serializer::serialize(transaction.smartContract));
    }
    else {
        if (!transaction.userFields.empty()) {
            size_t s = transaction.userFields.size();
            if (s != 0) {
                cs::Bytes msg;
                msg.resize(s);
                std::copy(transaction.userFields.data(), transaction.userFields.data() + s, msg.data());
                cs::IDataStream stream(msg.data(), msg.size());
                cs::Byte flagg;
                stream >> flagg;
                if (flagg) {
                    cTransaction.add_user_field(cs::trx_uf::ordinary::Text, transaction.userFields);
                }
                else {
                    uint8_t cnt;
                    stream >> cnt;
                    while (cnt > 0) {
                        uint32_t uf_id;
                        cs::Byte uf_type;
                        cs::Bytes data;
                        size_t iValue;
                        int32_t aInteger;
                        uint64_t aFraction;
                        std::string sValue;
                        stream >> uf_id >> uf_type;

                        switch (uf_type) {
                        case csdb::UserField::Type::Unknown:
                            data.clear();
                            stream >> data;
                            cTransaction.add_user_field(uf_id, transaction.userFields);
                            break;
                        case csdb::UserField::Type::Integer:
                            iValue = 0;
                            stream >> iValue;
                            cTransaction.add_user_field(uf_id, iValue);
                            break;
                        case csdb::UserField::Type::String:
                            uint32_t sSize;
                            stream >> sSize;
                            sValue.clear();
                            sValue += std::string{ stream.data(), sSize };
                            stream.skip(sSize);
                            cTransaction.add_user_field(uf_id, sValue);
                            break;
                        case csdb::UserField::Type::Amount:
                            aInteger = 0;
                            aFraction = 0;
                            stream >> aInteger >> aFraction;
                            cTransaction.add_user_field(uf_id, csdb::Amount{ aInteger, aFraction });
                            break;
                        default:
                            //TODO: add code here to aviod unknownUserfields in transactions
                            break;
                        }

                        --cnt;
                    }
                }
            }
        }
    }

    // for payable
    if (transaction.__isset.usedContracts && !transaction.usedContracts.empty() && !transaction.__isset.smartContract) {
        std::string uf;
        for (auto& addr : transaction.usedContracts) {
            uf += addr;
        }
        cTransaction.add_user_field(cs::trx_uf::ordinary::UsedContracts, uf);
    }

    if (!solver_.isTransactionsInputAvailable()) {
        auto msg = " Node is not syncronized or last round duration is too long.";
        return msg;
    }
    auto bb = cTransaction.to_byte_stream_for_sig();
    auto st = cs::Utils::byteStreamToHex(bb.data(), bb.size());
    cs::IterValidator::SimpleValidator::RejectCode err;
    csdb::AmountCommission countedFee;
    if (!cs::IterValidator::SimpleValidator::validate(cTransaction, blockchain_, solver_.smart_contracts(), &countedFee, &err)) {
        if (err == cs::IterValidator::SimpleValidator::kContractViolation) {
            std::string s = cs::SmartContracts::violations_message(solver_.smart_contracts().test_violations(cTransaction));
            if (!s.empty()) {
                return std::make_optional(s);
            }
        }
        auto msg = cs::IterValidator::SimpleValidator::getRejectMessage(err);
        if (err == cs::IterValidator::SimpleValidator::kInsufficientMaxFee) {
            msg += " Counted fee will be " + std::to_string(countedFee.to_double()) + ".";
        }
        return std::make_optional(std::move(msg));
    }

    return std::nullopt;
}


void APIHandler::checkTransactionsFlow(const cs::TransactionsPacket& packet, cs::DumbCv::Condition condition) {
    const auto& transactions = packet.transactions();

    if (transactions.empty()) {
        return;
    }

    for (const auto& transaction : transactions) {
        if (is_smart(transaction) || is_smart_state(transaction) || solver_.smart_contracts().is_payable_call(transaction)) {
            auto hashStateInst(lockedReference(this->hashStateSL));
            auto& item = (*hashStateInst)[transaction.signature()];

            if (!item) {
                item = std::make_shared<smartHashStateEntry>();
            }

            item->updateHash([&](const HashState&) {
                HashState res;
                res.condition = condition;
                return res;
            });
        }
        else {
            dumbCv_.sendCvSignal(transaction.signature(), condition, transaction.id(), csdb::Amount(transaction.counted_fee().to_double()));
        }
    }
}

void APIHandler::smartTransactionFlow(api::TransactionFlowResult& _return, const ::api::Transaction& transaction, csdb::Transaction& send_transaction) {
    auto input_smart = transaction.__isset.smartContract ? transaction.smartContract : SmartContractInvocation{};
    //auto send_transaction = makeTransaction(transaction);
    const auto smart_addr = blockchain_.getAddressByType(send_transaction.target(), BlockChain::AddressType::PublicKey);
    bool deploy = transaction.__isset.smartContract ? is_smart_deploy(input_smart) : false;

    std::vector<general::ByteCodeObject> origin_bytecode;
    if (!deploy) {
        for (auto& it : input_smart.smartContractDeploy.byteCodeObjects) {
            it.byteCode.clear();
        }
        input_smart.smartContractDeploy.sourceCode.clear();
        decltype(auto) lockedSmartOrigin = lockedReference(this->smart_origin);
        auto it = lockedSmartOrigin->find(smart_addr);
        if (it != lockedSmartOrigin->end()) {
            origin_bytecode = fetch_smart(executor_.loadTransactionApi(it->second)).smartContractDeploy.byteCodeObjects;
        }
        else {
            SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
            return;
        }
    }
    else {
        csdb::Address addr = blockchain_.getAddressByType(send_transaction.target(), BlockChain::AddressType::PublicKey);
        csdb::Address deployer = blockchain_.getAddressByType(send_transaction.source(), BlockChain::AddressType::PublicKey);
        auto scKey = cs::SmartContracts::get_valid_smart_address(deployer, uint64_t(send_transaction.innerID()), input_smart.smartContractDeploy);
        if (scKey != addr) {
            _return.status.code = int8_t(ERROR_CODE);
            const auto data = scKey.public_key().data();
            std::string str = EncodeBase58(data, data + cscrypto::kPublicKeySize);
            _return.status.message = "Bad smart contract address, expected " + str;
            return;
        }
    }

    auto hashStateEntry = [this, &send_transaction]() -> decltype(auto) {
        auto hashStateInst(lockedReference(this->hashStateSL));
        auto& item = (*hashStateInst)[send_transaction.signature()];

        if (!item) {
            item = std::make_shared<smartHashStateEntry>();
        }

        return item;
    }();

    hashStateEntry->getPosition();

    if (input_smart.forgetNewState) {
        auto source_pk = blockchain_.getAddressByType(send_transaction.source(), BlockChain::AddressType::PublicKey).to_api_addr();
        auto target_pk = blockchain_.getAddressByType(send_transaction.target(), BlockChain::AddressType::PublicKey).to_api_addr();
        executor::ExecuteByteCodeResult api_resp;
        const std::vector<general::ByteCodeObject>& bytecode = deploy ? input_smart.smartContractDeploy.byteCodeObjects : origin_bytecode;
        if (!deploy || !input_smart.smartContractDeploy.byteCodeObjects.empty()) {
            std::vector<executor::MethodHeader> methodHeaders;
            {
                executor::MethodHeader tmp;
                tmp.methodName = input_smart.method;
                tmp.params = input_smart.params;
                methodHeaders.push_back(tmp);
            }
            auto smartAddr = blockchain_.getAddressByType(send_transaction.target(), BlockChain::AddressType::PublicKey);
            std::string contract_state = cs::SmartContracts::get_contract_state(blockchain_, smartAddr);

            executor_.executeByteCode(api_resp, source_pk, target_pk, bytecode, contract_state, methodHeaders, true, cs::Executor::kUseLastSequence);
            if (api_resp.status.code) {
                _return.status.code = api_resp.status.code;
                _return.status.message = api_resp.status.message;
                hashStateEntry->yield();
                return;
            }
            _return.__isset.smart_contract_result = api_resp.__isset.results;
            if (_return.__isset.smart_contract_result && !api_resp.results.empty())
                _return.__set_smart_contract_result(api_resp.results[0].ret_val);
        }

        SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
        hashStateEntry->yield();
        return;
    }

    cs::Conveyer::instance().addTransaction(send_transaction);

    cs::Hash hashState;
    csdb::TransactionID newTransactionId{};

    cs::DumbCv::Condition condition = cs::DumbCv::Condition::Success;

    std::string retVal;
    auto resWait = hashStateEntry->waitTillFront([&](HashState& ss) {
        if (condition = ss.condition; condition != cs::DumbCv::Condition::Success)
            return true;

        if (!ss.condFlg) {
            return false;
        }

        hashState = ss.hash;
        retVal = ss.retVal;
        ss.condFlg = false;
        newTransactionId = ss.id;

        return true;
    });

    {
        auto hashStateInst(lockedReference(this->hashStateSL));
        hashStateInst->erase(send_transaction.signature());
    }

    if (!resWait) { // time is over
        SetResponseStatus(_return.status, APIRequestStatusType::INPROGRESS);
        return;
    }

    if (condition == cs::DumbCv::Condition::Expired) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE, "transaction is expired");
        return;
    }

    if (condition == cs::DumbCv::Condition::Rejected) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE, "transaction is rejected by consensus");
        return;
    }

    if (hashState == cs::Zero::hash) {
        _return.status.code = int8_t(ERROR_CODE);
        const std::string op = deploy ? "deployment of" : "call to";
        _return.status.message = op + " contract failed";
        return;
    }

    if (! deploy && !retVal.empty()) {
        _return.__set_smart_contract_result(cs::Serializer::deserialize<::general::Variant>(std::move(retVal)));
    }
    
    _return.id.poolSeq = newTransactionId.pool_seq();
    _return.id.index = static_cast<int32_t>(newTransactionId.index());

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS, getDelimitedTransactionSigHex(send_transaction));
}

void APIHandler::TransactionFlow(api::TransactionFlowResult& _return, const Transaction& transaction) {
    _return.roundNum = static_cast<int32_t>(cs::Conveyer::instance().currentRoundTable().round); // possible overflow
    csdb::Transaction transactionToSend;
    if (auto errInfo = checkTransaction(transaction, transactionToSend); errInfo.has_value()) {
        _return.status.code = int8_t(ERROR_CODE);
        _return.status.message = errInfo.value();
        return;
    }

    if(!transaction.__isset.smartContract && !solver_.smart_contracts().is_payable_call(transactionToSend))
        dumbTransactionFlow(_return, transactionToSend);
    else
        smartTransactionFlow(_return, transaction, transactionToSend);
}

void APIHandler::PoolListGet(api::PoolListGetResult& _return, const int64_t offset, const int64_t const_limit) {
    cs::Sequence limit = static_cast<cs::Sequence>(limitPage(const_limit));

    uint64_t sequence = blockchain_.getLastSeq();
    if (uint64_t(offset) > sequence) {
        return;
    }

    _return.pools.reserve(limit);

    PoolListGetStable(_return, static_cast<int64_t>(sequence - cs::Sequence(offset)), static_cast<int64_t>(limit));
    _return.count = int32_t(sequence + 1);
}

void APIHandler::PoolTransactionsGet(PoolTransactionsGetResult& _return, const int64_t sequence, const int64_t offset, const int64_t const_limit) {
    auto limit = limitPage(const_limit);
    csdb::Pool pool = executor_.loadBlockApi(cs::Sequence(sequence));

    if (pool.is_valid()) {
        _return.transactions = extractTransactions(pool, limit, offset);
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::PoolInfoGet(PoolInfoGetResult& _return, const int64_t sequence, const int64_t index) {
    csunused(index);
    csdb::Pool pool = executor_.loadBlockApi(cs::Sequence(sequence));
    _return.isFound = pool.is_valid();

    if (_return.isFound) {
        _return.pool = convertPool(pool);
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::StatsGet(api::StatsGetResult& _return) {
#ifdef USE_DEPRECATED_STATS //MONITOR_NODE
    csstats::StatsPerPeriod stats_inst = this->stats.getStats();

    for (auto& s : stats_inst) {
        api::PeriodStats ps = {};
        ps.periodDuration = s.periodSec;
        ps.poolsCount = s.poolsCount;
        ps.transactionsCount = s.transactionsCount;
        ps.smartContractsCount = s.smartContractsCount;
        ps.transactionsSmartCount = s.transactionsSmartCount;

        for (auto& t : s.balancePerCurrency) {
            api::CumulativeAmount amount;
            amount.integral = t.second.integral;
            amount.fraction = t.second.fraction;
            ps.balancePerCurrency[t.first] = amount;
        }

        _return.stats.push_back(ps);
    }
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
#else
    SetResponseStatus(_return.status, APIRequestStatusType::NOT_IMPLEMENTED);
#endif
}

void APIHandler::SmartContractGet(api::SmartContractGetResult& _return, const general::Address& address) {
    auto smartrid = [&]() -> decltype(auto) {
        auto smart_origin = lockedReference(this->smart_origin);
        auto it = smart_origin->find(BlockChain::getAddressFromKey(address));
        return it == smart_origin->end() ? csdb::TransactionID() : it->second;
    }();

    if (!smartrid.is_valid()) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
        return;
    }

    const csdb::Address adrs = BlockChain::getAddressFromKey(address);
    auto transaction = solver_.smart_contracts().get_contract_deploy(adrs);

    if (!transaction.is_valid()) {
        transaction = executor_.loadTransactionApi(smartrid);
    }

    _return.smartContract = fetch_smart_body(transaction);
    _return.smartContract.objectState = cs::SmartContracts::get_contract_state(blockchain_, adrs);

    if (_return.smartContract.address.empty()) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
    }
    else if (cs::SmartContracts::get_contract_state(blockchain_, transaction.target()).empty()) {
        SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
    }
    else {
        SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
    }

    return;
}

void APIHandler::FilteredTransactionsListGet(api::FilteredTransactionsListResult& _return, const api::TransactionsQuery& generalQuery) {
    cslog() << __func__;
    auto gq = generalQuery;
    auto queriesLimit = limitQueries(gq.queries.size(), MaxQueriesNumber);
    auto limit = int64_t(1000);
    cslog() << "Limit Queries = " << queriesLimit;
    uint16_t flagg = gq.flag;
    size_t cnt = 0;
    for (auto currentQuery : gq.queries)
    {
        cslog() << "Limit Transactions = " << limit;
        if (queriesLimit == 0 || limit ==0) {
            break;
        }
        cslog() << "Counter = " << cnt;
        const csdb::Address addr = BlockChain::getAddressFromKey(currentQuery.requestedAddress);
        BlockChain::Transactions transactions;

        auto id = csdb::TransactionID(currentQuery.fromId.poolSeq, currentQuery.fromId.index);
        blockchain_.getTransactionsUntill(transactions, addr, id, limit, flagg);

        cslog() << "Trx(" << cnt << ") = " << transactions.size();
        api::PublicKeyTransactions singleResponse;
        singleResponse.requestedAddress = currentQuery.requestedAddress;
        size_t tcnt = 0;
        for (auto it : transactions) {
            ++tcnt;
            if (limit > 0) {
                --limit;
            }
            cslog() << "Converting tr(" << tcnt << ")";
            singleResponse.transactions.push_back(convertTransactionToShort(it));
        }
        cslog() << "Conversion finished";

        std::vector<std::tuple<csdb::Address, std::string, std::string>> tokensList;
        tm_.loadTokenInfo([&addr, &tokensList](const TokensMap& tokens, const HoldersMap& holders){
            auto holderIt = holders.find(addr);
            if (holderIt != holders.end()) { 
                for (const auto& tokAddr : holderIt->second) {
                    auto it = tokens.find(tokAddr);
                    if (it == tokens.end()) {
                        continue;
                    }
                    tokensList.push_back(std::make_tuple(tokAddr, it->second.symbol, it->second.name));
                }
            }
        });
        cslog() << "Token List size: " << tokensList.size();
        size_t offset = 0;
        size_t singleLimit = 99;
        std::vector<api::SelectedTokenTransfers> sumTransfers;
        for (auto itToken : tokensList) {
            api::TokenTransfersResult transferResult;
            api::SelectedTokenTransfers transfersListLocal;
            auto tAddr = fromByteArray(std::get<0>(itToken).public_key());
            tokenTransactionsInternal(transferResult, *this, tm_, tAddr, true, true, offset, singleLimit, addr);
            transfersListLocal.__set_tokenAddress(tAddr);
            transfersListLocal.__set_tokenTiker(std::get<1>(itToken));
            transfersListLocal.__set_tokenName(std::get<2>(itToken));
            size_t token_cnt = 0;
            cslog() << "Token start. Transfers found: " << transferResult.transfers.size();
            for (auto it : transferResult.transfers) {
                ++token_cnt;
                cslog() << "Adding transfer: " << token_cnt;
                transfersListLocal.transfers.push_back(it);
            }
            sumTransfers.push_back(transfersListLocal);
            cslog() << "Token finished: " << transfersListLocal.transfers.size() << " transfers, transferList# " << sumTransfers.size();
        }
        if (sumTransfers.size() > 0) {
            singleResponse.__set_transfersList(sumTransfers);
        }

        if (singleResponse.transactions.size() > 0 || singleResponse.transfersList.size() > 0) {
            cslog() << "Single Response: trx: " << singleResponse.transactions.size() << ", " << singleResponse.transfersList.size();
            _return.queryResponse.push_back(singleResponse);
            ++cnt;
            --queriesLimit;
        }

    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
    return;
}

void APIHandler::store_block_slot(const csdb::Pool& pool) {
    updateSmartCachesPool(pool);
}

void APIHandler::baseLoaded(const csdb::Pool& pool) {
    if (maxReadSequence && pool.sequence() == maxReadSequence) {
        isBDLoaded_ = true;
#ifdef TOKENS_CACHE   
        tm_.loadTokenInfo([&](const TokensMap& tokens, const HoldersMap&) {
            int count{}, i{};
            size_t tokenSize = tokens.size();
            cslog() << "tokens are loading(" << tokenSize << ")...";
            for (auto& tk : tokens) {
                if (tokenSize > 100 && !(i++ % (tokenSize / 10)))
                    cslog() << "loading tokens: " << 10 * (count++) << "%";
                tm_.refreshTokenState(tk.first, cs::SmartContracts::get_contract_state(get_s_blockchain(), tk.first), true);
            }
            cslog() << "tokens loaded!";
            });
#endif
    }
}

void APIHandler::maxBlocksCount(cs::Sequence lastBlockNum) {
    if (!lastBlockNum) {
        isBDLoaded_ = true;
    }

    maxReadSequence = lastBlockNum;
}

void APIHandler::onPacketExpired(const cs::TransactionsPacket& packet) {
    checkTransactionsFlow(packet, cs::DumbCv::Condition::Expired);
}

void APIHandler::onTransactionsRejected(const cs::TransactionsPacket& packet) {
    checkTransactionsFlow(packet, cs::DumbCv::Condition::Rejected);
}

void APIHandler::collect_all_stats_slot(const csdb::Pool& pool) {
#ifdef USE_DEPRECATED_STATS
    const ::csstats::Periods periods = ::csstats::collectionPeriods;

    static unsigned int currentCutIndex = 0;
    static auto startCutTime = stats_.first[currentCutIndex].timeStamp;
    static auto endCutTime = stats_.first[currentCutIndex + 1].timeStamp;

    auto now = std::chrono::system_clock::now();
    auto poolTime_t = atoll(pool.user_field(0).value<std::string>().c_str()) / 1000;
    auto poolTime = std::chrono::system_clock::from_time_t(poolTime_t);

    using Seconds = std::chrono::seconds;
    Seconds poolAgeSec = std::chrono::duration_cast<Seconds>(now - poolTime);

    if (startCutTime <= poolTime && poolTime < endCutTime) {
        ::csstats::PeriodStats& periodStats = stats_.first[currentCutIndex];
        ++periodStats.poolsCount;

        size_t transactionsCount = pool.transactions_count();
        periodStats.transactionsCount += static_cast<uint32_t>(transactionsCount);

        for (size_t i = 0; i < transactionsCount; ++i) {
            const auto& transaction = pool.transaction(csdb::TransactionID(pool.sequence(), i));

#ifdef MONITOR_NODE
            if (is_smart(transaction) || is_smart_state(transaction))
                ++periodStats.transactionsSmartCount;
#endif

            if (is_deploy_transaction(transaction)) {
                ++periodStats.smartContractsCount;
            }

            Currency currency = 1;

            const auto& amount = transaction.amount();

            periodStats.balancePerCurrency[cs::Byte(currency)].integral += amount.integral();
            periodStats.balancePerCurrency[cs::Byte(currency)].fraction += amount.fraction();
        }
    }
    else if ((currentCutIndex + 1) < stats_.first.size()) {
        startCutTime = stats_.first[currentCutIndex].timeStamp;
        endCutTime = stats_.first[currentCutIndex + 1].timeStamp;
        ++currentCutIndex;
    }

    auto period = csstats::Period(poolAgeSec.count());
    for (size_t periodIndex = 0; periodIndex < periods.size(); ++periodIndex) {
        if (period < periods[periodIndex]) {
            csstats::PeriodStats& periodStats = stats_.second[periodIndex];
            periodStats.poolsCount++;

            size_t transactionsCount = pool.transactions_count();
            periodStats.transactionsCount += static_cast<uint32_t>(transactionsCount);

            for (size_t i = 0; i < transactionsCount; ++i) {
                const auto& transaction = pool.transaction(csdb::TransactionID(pool.sequence(), i));

                if (transaction.source() == blockchain_.getGenesisAddress()) {
                    continue;
                }
#ifdef MONITOR_NODE
                if (is_smart(transaction) || is_smart_state(transaction)) {
                    ++periodStats.transactionsSmartCount;
                }
#endif

                if (is_deploy_transaction(transaction)) {
                    ++periodStats.smartContractsCount;
                }

                //Currency currency = currencies_indexed[transaction.currency().to_string()];
                Currency currency = 1;

                const auto& amount = transaction.amount();

                periodStats.balancePerCurrency[cs::Byte(currency)].integral += amount.integral();
                periodStats.balancePerCurrency[cs::Byte(currency)].fraction += amount.fraction();
            }
        }
    }
#else
    csunused(pool);
#endif // USE_DEPRECATED_STATS
}
//

bool APIHandler::updateSmartCachesTransaction(csdb::Transaction trxn, cs::Sequence sequence) {
    auto source_pk = blockchain_.getAddressByType(trxn.source(), BlockChain::AddressType::PublicKey);
    auto target_pk = blockchain_.getAddressByType(trxn.target(), BlockChain::AddressType::PublicKey);

    if (is_smart_state(trxn)) {
        cs::SmartContractRef scr(trxn.user_field(cs::trx_uf::new_state::RefStart));
        csdb::TransactionID trId(scr.sequence, scr.transaction);
        const auto execTrans = solver_.smart_contracts().get_contract_call(trxn);

        csdetails() << "[API]: state transaction found: " << trxn.id().pool_seq() << '.' << trxn.id().index()
            << " <- " << execTrans.id().pool_seq() << '.' << execTrans.id().index();

        if ((execTrans.is_valid() && is_smart(execTrans)) ||
            (execTrans.amount().to_double() > 0)) {
            const auto smart = fetch_smart(execTrans);

            if (!smart.method.empty()) {
                mExecuteCount_[smart.method]++;
            }

            {
                csdb::UserField uf = trxn.user_field(cs::trx_uf::new_state::RetVal);
                std::string retVal = uf.value<std::string>();
                ::general::Variant val;

                if (!retVal.empty()) {
                    val = cs::Serializer::deserialize<::general::Variant>(std::move(retVal));
                }

                auto opers = lockedReference(this->smart_operations);
                auto& op = (*opers)[trId];
                op.state = cs::SmartContracts::is_state_updated(trxn) ? SmartOperation::State::Success : SmartOperation::State::Failed;
                op.stateTransaction = trxn.id();

                csdetails() << "[API]: status of state " << trxn.id().pool_seq() << '.' << trxn.id().index() << " is " << static_cast<int>(op.state);

                auto sp = lockedReference(this->smarts_pending);// std::map<cs::Sequence, std::vector<csdb::TransactionID>>
                auto seq = execTrans.id().pool_seq();
                if (auto elm = sp->find(seq); elm != sp->end()) {
                    auto& vId = elm->second;
                    if (auto idIt = std::find(vId.begin(), vId.end(), trId); idIt != vId.end()) {
                        if (vId.erase(idIt) == vId.end()) {
                            sp->erase(elm);
                        }
                    }                   
                }

                if (!retVal.empty()) {
                    op.hasRetval = true;
                    if (val.__isset.v_boolean || val.__isset.v_boolean_box) {
                        op.returnsBool = true;
                        op.boolResult = val.__isset.v_boolean ? val.v_boolean : val.v_boolean_box;
                    }
                }
            }

            HashState res;
            res.hash = cs::Zero::hash;
            std::string newStateStr;
            std::string newHashStr;
            if (trxn.user_field_ids().count(cs::trx_uf::new_state::RetVal) > 0) {
                res.retVal = trxn.user_field(cs::trx_uf::new_state::RetVal).template value<std::string>();
            }

            general::Variant var;
            if (!res.retVal.empty()) {
                std::string tmp = res.retVal;
                var = cs::Serializer::deserialize<general::Variant>(std::move(tmp));
            }

            if (trxn.user_field_ids().count(cs::trx_uf::new_state::Value) > 0) {
                // new_state value, not hash!
                newStateStr = trxn.user_field(cs::trx_uf::new_state::Value).template value<std::string>();
            }
            else {
                newHashStr = trxn.user_field(cs::trx_uf::new_state::Hash).template value<std::string>();
                if (isBDLoaded_) { // signal to end waiting for a transaction
                    auto hashStateInst(lockedReference(this->hashStateSL));
                    auto& item = (*hashStateInst)[execTrans.signature()];

                    if (!item) {
                        item = std::make_shared<smartHashStateEntry>();
                    }

                    item->updateHash([&](const HashState& oldHash) {
                        if (!newHashStr.empty()) {
                            std::copy(newHashStr.begin(), newHashStr.end(), res.hash.begin());
                        }
                        else {
                            res.hash = cs::Zero::hash;
                        }

                        res.retVal = trxn.user_field(cs::trx_uf::new_state::RetVal).template value<std::string>();
                        res.isOld = (res.hash == oldHash.hash);
                        res.condFlg = true;
                        res.id = trxn.id();
                        return res;
                    });
                    csdetails() << "[API]: sended signal, state trx: " << trxn.id().pool_seq() << '.' << trxn.id().index()
                        << ", hash: " << cs::Utils::byteStreamToHex(newHashStr.data(), newHashStr.size());
                }
            }

            if (!newStateStr.empty() || !newHashStr.empty()) { // update tokens
                auto caller_pk = blockchain_.getAddressByType(execTrans.source(), BlockChain::AddressType::PublicKey);

                if (is_smart_deploy(smart)) {
                    tm_.checkNewDeploy(target_pk, caller_pk, smart);
                }

                if (newStateStr.empty()) {
                    newStateStr = cs::SmartContracts::get_contract_state(blockchain_, target_pk);
                }
                if (!newStateStr.empty()) {
                    tm_.checkNewState(target_pk, caller_pk, smart, newStateStr);
                }
            }
        }
    }
    else {
        csdetails() << "[API]: deploy/execute found: " << trxn.id().pool_seq() << '.' << trxn.id().index();
        {
            auto& e = [&]() -> decltype(auto) {
                auto smartLastTrxn = lockedReference(this->smartLastTrxn_);
                return (*smartLastTrxn)[target_pk];
            }();

            std::unique_lock lock(e.lock);
            e.trid_queue.push_back(trxn.id().clone());
            e.new_trxn_cv.notify_all();
        }

        {
            auto opers = lockedReference(this->smart_operations);
            (*opers)[trxn.id()];

            auto sp = lockedReference(this->smarts_pending);
            (*sp)[sequence].push_back(trxn.id());
        }

        const auto smart = fetch_smart(trxn);
        if (is_smart_deploy(smart)) {
            if (!smart.smartContractDeploy.byteCodeObjects.empty()) {
                auto lockedSmartOrigin = lockedReference(this->smart_origin);
                (*lockedSmartOrigin)[target_pk] = trxn.id().clone();
                executor_.updateDeployTrxns(target_pk, trxn.id().clone());
            }
            auto locked_deployed_by_creator = lockedReference(this->deployedByCreator_);
            (*locked_deployed_by_creator)[source_pk].push_back(trxn.id().clone());
        }
        return true;
    }
    return false;
}

void APIHandler::updateSmartCachesPool(const csdb::Pool& pool) {
    static int cleanCount = 0;
    static const int MAX_ROUND_WAITING = 100;

    if ((cleanCount++) > MAX_ROUND_WAITING) {
        auto smartsOperns   = lockedReference(this->smart_operations);
        auto smartsPending  = lockedReference(this->smarts_pending);

        for (auto iter = smartsPending->begin(); iter != smartsPending->end();) {
            auto&[seq, vId] = *iter;

            if (pool.sequence() - seq < MAX_ROUND_WAITING) {
                break;
            }

            for (auto& id : vId) {
                if ((*smartsOperns)[id].state == SmartOperation::State::Pending) {
                    (*smartsOperns)[id].state = SmartOperation::State::Failed;
                }
            }

            iter = smartsPending->erase(iter);
        }

        cleanCount = 0;
    }

    if (!pool.is_valid() || !pool.transactions_count()) {
        return;
    }

    for (auto& trx : pool.transactions()) {
        if (is_smart(trx) || is_smart_state(trx) || solver_.smart_contracts().is_payable_call(trx)) {
            updateSmartCachesTransaction(trx, pool.sequence());
        }
        else { // if dumb transaction
            dumbCv_.sendCvSignal(trx.signature(), cs::DumbCv::Condition::Success, trx.id(), csdb::Amount(trx.counted_fee().to_double()));
        }
    }
}

template <typename Mapper>
size_t APIHandler::getMappedDeployerSmart(const csdb::Address& deployer, Mapper mapper, std::vector<decltype(mapper(api::SmartContract()))>& out, int64_t offset, int64_t limit) {
    auto lockedDeployedByCreator = lockedReference(this->deployedByCreator_);
    auto& element = (*lockedDeployedByCreator)[deployer];

    if (offset >= static_cast<int64_t>(element.size())) { // Offset is more than number of smart!"
        return 0;
    }

    if (offset + limit > static_cast<int64_t>(element.size())) {
        limit = static_cast<int64_t>(element.size()) - offset;
    }

    if (offset == 0 && limit == 0) {
        limit = static_cast<int64_t>(element.size());
    }

    auto begIt = element.begin() + offset;
    for (auto trid = begIt; trid != begIt + limit && trid != element.end(); ++trid) {
        auto tr = executor_.loadTransactionApi(*trid);

        if (cs::SmartContracts::get_contract_state(blockchain_, tr.target()).empty()) {
            continue;
        }

        auto smart = fetch_smart_body(tr);
        out.push_back(mapper(smart));
    }

    return element.size();
}

void APIHandler::SmartContractsListGet(api::SmartContractsListGetResult& _return, const general::Address& deployer, const int64_t offset, const int64_t limit) {
    const csdb::Address addr = BlockChain::getAddressFromKey(deployer);

    _return.count = static_cast<decltype(_return.count)>(getMappedDeployerSmart(addr, [](const api::SmartContract& smart) {
        return smart;
    }, _return.smartContractsList, offset, limit));

    SetResponseStatus(_return.status, _return.smartContractsList.empty() ? APIRequestStatusType::NOT_FOUND : APIRequestStatusType::SUCCESS);
}

void APIHandler::SmartContractAddressesListGet(api::SmartContractAddressesListGetResult& _return, const general::Address& deployer) {
    const csdb::Address addr = BlockChain::getAddressFromKey(deployer);

    getMappedDeployerSmart(addr, [](const SmartContract& sc) {
        return sc.address;
    }, _return.addressesList);

    SetResponseStatus(_return.status, _return.addressesList.empty() ? APIRequestStatusType::NOT_FOUND : APIRequestStatusType::SUCCESS);
}

void APIHandler::GetLastHash(api::PoolHash& _return) {
    _return = fromByteArray(blockchain_.getLastHash().to_binary());
    return;
}

void APIHandler::PoolListGetStable(api::PoolListGetResult& _return, const int64_t sequence, const int64_t const_limit) {
    auto limit = limitPage(const_limit);
    if (sequence < 0) {
        return;
    }
    cs::Sequence seq = cs::Sequence(sequence);
    csmeta(csdebug) << "sequence " << seq << ", limit " << limit;
    bool limSet = false;

    while (limit) {
        auto lockedPoolCache = lockedReference(this->poolCache);
        auto cch = lockedPoolCache->find(seq);

        if (cch == lockedPoolCache->end()) {
            auto pool = executor_.loadBlockApi(seq);
            if (pool.is_valid()) {
                api::Pool apiPool = convertPool(pool);
                _return.pools.push_back(apiPool);
                lockedPoolCache->insert(cch, std::make_pair(seq, apiPool));
                if (!limSet) {
                    _return.count = int32_t(seq + 1);
                    limSet = true;
                }
            }
        }
        else {
            _return.pools.push_back(cch->second);
            if (!limSet) {
                _return.count = int32_t(cch->second.poolNumber + 1);
                limSet = true;
            }
        }

        --seq;
        --limit;
    }
}

void APIHandler::WaitForSmartTransaction(api::TransactionId& _return, const general::Address& smart_public) {
    csdb::Address key = BlockChain::getAddressFromKey(smart_public);
    decltype(smartLastTrxn_)::LockedType::iterator it;
    auto& entry = [&]() -> decltype(auto) {
        auto smartLastTrxn = lockedReference(this->smartLastTrxn_);
        std::tie(it, std::ignore) = smartLastTrxn->emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple());
        return std::ref(it->second).get();
    }();

    {
        std::unique_lock lock(entry.lock);
        ++entry.awaiter_num;
        const auto checker = [&]() {
            if (!entry.trid_queue.empty()) {
                _return = convert_transaction_id(entry.trid_queue.front());
                if (--entry.awaiter_num == 0) {
                    entry.trid_queue.pop_front();
                }
                return true;
            }
            return false;
        };
        entry.new_trxn_cv.wait(lock, checker);
    }
}

void APIHandler::SmartContractsAllListGet(SmartContractsListGetResult& _return, const int64_t _offset, const int64_t _limit) {
    auto offset = _offset;
    auto limit = limitPage(_limit);
    auto lockedSmartOrigin = lockedReference(this->smart_origin);
    _return.count = static_cast<int32_t>(lockedSmartOrigin->size());

    for (const auto&[addr, id] : *lockedSmartOrigin) {
        if (offset) --offset;
        else if (limit) {
            auto tr = solver_.smart_contracts().get_contract_deploy(addr);
            if (!tr.is_valid())
                tr = executor_.loadTransactionApi(id);
            _return.smartContractsList.push_back(fetch_smart_body(tr));
            --limit;
        }
        else break;
    }
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void api::APIHandler::WaitForBlock(PoolHash& _return, const PoolHash& /* obsolete */) {
    std::unique_lock lock(dbLock_);
    newBlockCv_.wait(lock);
    _return = fromByteArray(blockchain_.getLastHash().to_binary());
}

void APIHandler::TransactionsStateGet(TransactionsStateGetResult& _return, const general::Address& address, const std::vector<int64_t>& v) {
    csunused(v);
    csunused(address);
    _return.roundNum = static_cast<int32_t>(cs::Conveyer::instance().currentRoundTable().round);   // possible soon round overflow
    SetResponseStatus(_return.status, APIRequestStatusType::NOT_IMPLEMENTED);
}

void api::APIHandler::SmartMethodParamsGet(SmartMethodParamsGetResult& _return, const general::Address& address, const int64_t id) {
    csunused(id);
    csunused(address);
    SetResponseStatus(_return.status, APIRequestStatusType::NOT_IMPLEMENTED);
}

void APIHandler::ContractAllMethodsGet(ContractAllMethodsGetResult& _return, const std::vector<general::ByteCodeObject>& byteCodeObjects) {
    executor::GetContractMethodsResult executor_ret;
    if (byteCodeObjects.empty()) {
        return;
    }
    executor_.getContractMethods(executor_ret, byteCodeObjects);
    _return.code = executor_ret.status.code;
    _return.message = executor_ret.status.message;
    auto allMethods = executor_ret.methods;
    for (auto met : allMethods) {
        //_return.methods[Count].name = executor_ret.methods[Count].name;
        //for (size_t SubCount = 0; SubCount < executor_ret.methods[Count].arguments.size(); SubCount++) {
        //    _return.methods[Count].arguments[SubCount].type = executor_ret.methods[Count].arguments[SubCount].type;
        //    _return.methods[Count].arguments[SubCount].name = executor_ret.methods[Count].arguments[SubCount].name;
        //}
        //_return.methods[Count].returnType = executor_ret.methods[Count].returnType;
        ::general::MethodDescription method;
        method.name = met.name;
        method.returnType = met.returnType;
        for (auto arg : met.arguments) {
            ::general::MethodArgument argument;
            argument.type = arg.type;
            argument.name = arg.name;
            method.arguments.push_back(argument);
        }
        _return.methods.push_back(method);
    }
}

void APIHandler::addTokenResult(api::TokenTransfersResult& _return, const csdb::Address& token, const std::string& code, const csdb::Pool& pool, const csdb::Transaction& tr,
                                const api::SmartContractInvocation& smart, const std::pair<csdb::Address, csdb::Address>& addrPair) {
    api::TokenTransfer transfer;
    transfer.token = fromByteArray(token.public_key());
    transfer.code = code;
    transfer.sender = fromByteArray(addrPair.first.public_key());
    transfer.receiver = fromByteArray(addrPair.second.public_key());
    transfer.amount = TokensMaster::getAmount(smart);
    transfer.initiator = fromByteArray(blockchain_.getAddressByType(tr.source(), BlockChain::AddressType::PublicKey).public_key());
    transfer.transaction.poolSeq = static_cast<int64_t>(tr.id().pool_seq());
    transfer.transaction.index = static_cast<int32_t>(tr.id().index());
    transfer.time = atoll(pool.user_field(0).value<std::string>().c_str());

    auto opers = cs::lockedReference(this->smart_operations);
    transfer.state = static_cast<SmartOperationState>((*opers)[tr.id()].state);

    if (transfer.state == SOS_Success) {
        _return.transfers.push_back(transfer);
    }
}

void APIHandler::addTokenResult(api::TokenTransactionsResult& _return, const csdb::Address& token, const std::string&, const csdb::Pool& pool, const csdb::Transaction& tr,
                    const api::SmartContractInvocation& smart, const std::pair<csdb::Address, csdb::Address>&) {
    api::TokenTransaction trans;
    trans.token = fromByteArray(token.public_key());
    trans.transaction.poolSeq = static_cast<int64_t>(tr.id().pool_seq());
    trans.transaction.index = static_cast<int32_t>(tr.id().index());
    trans.time = atoll(pool.user_field(0).value<std::string>().c_str());
    trans.initiator = fromByteArray(blockchain_.getAddressByType(tr.source(), BlockChain::AddressType::PublicKey).public_key());
    trans.method = smart.method;
    trans.params = smart.params;

    auto opers = cs::lockedReference(this->smart_operations);
    trans.state = static_cast<SmartOperationState>((*opers)[tr.id()].state);

    _return.transactions.push_back(trans);
}

void putTokenInfo(api::TokenInfo& ti, const general::Address& addr, const Token& token) {
    ti.address = addr;
    ti.code = token.symbol;
    ti.name = token.name;
    ti.totalSupply = token.totalSupply;
    ti.owner = fromByteArray(token.owner.public_key());
    ti.transfersCount = int32_t(token.transfersCount);
    ti.transactionsCount = int32_t(token.transactionsCount);
    ti.holdersCount = int32_t(token.realHoldersCount);
    ti.tokenStandard = decltype(api::TokenInfo::tokenStandard)(uint32_t((token.tokenStandard)));
}

template <typename ResultType>
void APIHandler::tokenTransactionsInternal(ResultType& _return, APIHandler& handler, TokensMaster& tm_, const general::Address& token, bool transfersOnly, bool filterByWallet, int64_t offset,
                               int64_t limit, const csdb::Address& wallet) {
    cslog() << __func__;
    if (!validatePagination(_return, handler, offset, limit)) {
        return;
    }

    const csdb::Address addr = BlockChain::getAddressFromKey(token);
    bool tokenFound = false;
    std::string code;

    tm_.loadTokenInfo([&addr, &tokenFound, &transfersOnly, &filterByWallet, &code, &wallet, &_return](const TokensMap& tm_, const HoldersMap&) {
        cslog() << __func__ << ": load tokenInfo";
        auto it = tm_.find(addr);
        tokenFound = !(it == tm_.end());
        if (tokenFound) {
            cslog() << __func__ << ": Tokens found";
            code = it->second.symbol;
            if (transfersOnly && !filterByWallet) {
                _return.count = static_cast<int32_t>(it->second.transfersCount);
            }
            else if (!transfersOnly) {
                _return.count = static_cast<int32_t>(it->second.transactionsCount);
            }
            else if (transfersOnly && filterByWallet) {
                auto hIt = it->second.holders.find(wallet);
                if (hIt != it->second.holders.end()) {
                    _return.count = static_cast<int32_t>(hIt->second.transfersCount);
                }
                else {
                    _return.count = 0;
                }
            }
        }
    });

    if (!tokenFound) {
        handler.SetResponseStatus(_return.status, APIHandlerBase::APIRequestStatusType::FAILURE);
        return;
    }
    cslog() << __func__ << ": Starting iterations";
    handler.iterateOverTokenTransactions(addr, [&](const csdb::Pool& pool, const csdb::Transaction& tr) {
        auto smart = fetch_smart(tr);
        if (transfersOnly && !TokensMaster::isTransfer(smart.method, smart.params)) {
            return true;
        }

        csdb::Address addr_pk = handler.get_s_blockchain().getAddressByType(tr.source(), BlockChain::AddressType::PublicKey);
        auto addrPair = TokensMaster::getTransferData(addr_pk, smart.method, smart.params);

        if (filterByWallet && addrPair.first != wallet && addrPair.second != wallet) {
            return true;
        }

        if (--offset >= 0) {
            return true;
        }

        addTokenResult(_return, addr, code, pool, tr, smart, addrPair);
        return !(--limit == 0);
    });

    handler.SetResponseStatus(_return.status, APIHandlerBase::APIRequestStatusType::SUCCESS);
}

void APIHandler::iterateOverTokenTransactions(const csdb::Address& addr, const std::function<bool(const csdb::Pool&, const csdb::Transaction&)> func) {
    std::list<csdb::TransactionID> l_id;
    for (auto trIt = cs::TransactionsIterator(blockchain_, addr); trIt.isValid(); trIt.next()) {
        if (is_smart_state(*trIt)) {
            cs::SmartContractRef smart_ref;
            smart_ref.from_user_field(trIt->user_field(cs::trx_uf::new_state::RefStart));
            l_id.emplace_back(csdb::TransactionID(smart_ref.sequence, smart_ref.transaction));
        }
        else if (is_smart(*trIt)) {
            auto it = std::find(l_id.begin(), l_id.end(), trIt->id());
            if (it != l_id.end()) {
                l_id.erase(it);
                if (!func(trIt.getPool(), *trIt)) {
                    break;
                }
            }
        }
    }
}

api::SmartContractInvocation APIHandler::getSmartContract(const csdb::Address& addr, bool& present) {
    csdb::Address abs_addr = addr;
    if (addr.is_wallet_id()) {
        abs_addr = blockchain_.getAddressByType(addr, BlockChain::AddressType::PublicKey);
    }

    const auto deploy = solver_.smart_contracts().get_contract_deploy(addr);
    if (deploy.is_valid()) {
        present = true;
        return fetch_smart(deploy);
    }

    return api::SmartContractInvocation{};
}

std::vector<general::ByteCodeObject> APIHandler::getSmartByteCode(const csdb::Address& addr, bool& present) {
    auto invocation = getSmartContract(addr, present);
    return present ? invocation.smartContractDeploy.byteCodeObjects : std::vector<general::ByteCodeObject>{};
}

void APIHandler::SmartContractCompile(api::SmartContractCompileResult& _return, const std::string& sourceCode) {
    executor::CompileSourceCodeResult result;
    executor_.compileSourceCode(result, sourceCode);

    if (result.status.code) {
        _return.status.code = result.status.code;
        _return.status.message = result.status.message;
        return;
    }

    executor::GetContractMethodsResult methodsResult;
    if (result.byteCodeObjects.empty()) {
        return;
    }
    executor_.getContractMethods(methodsResult, result.byteCodeObjects);

    if (methodsResult.status.code) {
        _return.status.code = methodsResult.status.code;
        _return.status.message = methodsResult.status.message;
        return;
    }

    _return.tokenStandard = int32_t(methodsResult.tokenStandard);
    _return.byteCodeObjects = std::move(result.byteCodeObjects);

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::SmartContractDataGet(api::SmartContractDataResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);

    bool present = false;
    std::vector<general::ByteCodeObject> byteCode = getSmartByteCode(addr, present);
    std::string state = cs::SmartContracts::get_contract_state(blockchain_, addr);

    if (!present || state.empty()) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
        return;
    }

    executor::GetContractMethodsResult methodsResult;
    if (byteCode.empty())
        return;
    executor_.getContractMethods(methodsResult, byteCode);

    if (methodsResult.status.code) {
        _return.status.code = methodsResult.status.code;
        _return.status.message = methodsResult.status.message;
        return;
    }

    executor::GetContractVariablesResult variablesResult;
    if (byteCode.empty())
        return;
    executor_.getContractVariables(variablesResult, byteCode, state);

    if (variablesResult.status.code) {
        _return.status.code = variablesResult.status.code;
        _return.status.message = variablesResult.status.message;
        return;
    }

    for (auto& m : methodsResult.methods) {
        api::SmartContractMethod scm;
        scm.returnType = std::move(m.returnType);
        scm.name = std::move(m.name);
        for (auto& at : m.arguments) {
            api::SmartContractMethodArgument scma;

            scma.type = at.type;
            scma.name = at.name;

            scm.arguments.push_back(scma);
        }

        _return.methods.push_back(scm);
    }

    _return.variables = variablesResult.contractVariables;
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::ExecuteCountGet(ExecuteCountGetResult& _return, const std::string& executeMethod) {
    if (auto itCount = mExecuteCount_.find(executeMethod); itCount != mExecuteCount_.end()) {
        _return.executeCount = itCount->second;
        SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
    }
    else {
        SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
    }
}

void APIHandler::TokenBalancesGet(api::TokenBalancesResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    tm_.loadTokenInfo([&_return, &addr](const TokensMap& tokens, const HoldersMap& holders) {
        auto holderIt = holders.find(addr);
        if (holderIt != holders.end()) {
            for (const auto& tokAddr : holderIt->second) {
                auto tokenIt = tokens.find(tokAddr);
                if (tokenIt == tokens.end()) {
                    continue;  // This shouldn't happen
                }

                api::TokenBalance tb;
                tb.token = fromByteArray(tokenIt->first.public_key());
                tb.code = tokenIt->second.symbol;
                tb.name = tokenIt->second.name;

                auto hi = tokenIt->second.holders.find(addr);
                if (hi != tokenIt->second.holders.end()) {
                    tb.balance = hi->second.balance;
                }

                if (!TokensMaster::isZeroAmount(tb.balance)) {
                    _return.balances.push_back(tb);
                }
            }
        }
    });

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::TokenTransfersGet(api::TokenTransfersResult& _return, const general::Address& token, int64_t offset, int64_t limit) {
    tokenTransactionsInternal(_return, *this, tm_, token, true, false, offset, limit);
}

void APIHandler::TokenTransferGet(api::TokenTransfersResult& _return, const general::Address& token, const TransactionId& id) {
    const csdb::TransactionID trxn_id = csdb::TransactionID(cs::Sequence(id.poolSeq), cs::Sequence(id.index));
    const csdb::Transaction trxn = executor_.loadTransactionApi(trxn_id);
    const csdb::Address addr = BlockChain::getAddressFromKey(token);

    std::string code{};
    tm_.loadTokenInfo([&addr, &code](const TokensMap& tm_, const HoldersMap&) {
        const auto it = tm_.find(addr);
        if (it != tm_.cend()) {
            code = it->second.symbol;
        }
    });

    if (code.empty()) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
        return;
    }

    const auto pool = executor_.loadBlockApi(trxn.id().pool_seq());
    const auto smart = fetch_smart(trxn);
    const auto addr_pk = blockchain_.getAddressByType(trxn.source(), BlockChain::AddressType::PublicKey);
    const auto addrPair = TokensMaster::getTransferData(addr_pk, smart.method, smart.params);

    _return.count = 1;

    addTokenResult(_return, addr, code, pool, trxn, smart, addrPair);
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::TransactionsListGet(api::TransactionsGetResult& _return, int64_t offset, int64_t limit) {
    if (!validatePagination(_return, *this, offset, limit)) {
        return;
    }

    _return.result = false;
    _return.total_trxns_count = static_cast<int32_t>(blockchain_.getTransactionsCount());

    auto tPair = blockchain_.getLastNonEmptyBlock();
    while (limit > 0 && tPair.second) {
        if (tPair.second <= offset) {
            offset -= tPair.second;
        }
        else {
            auto p = executor_.loadBlockApi(tPair.first);
            auto it = p.transactions().rbegin() + offset;
            offset = 0;

            while (it != p.transactions().rend() && limit > 0) {
                it->set_time(BlockChain::getBlockTime(p));
                _return.transactions.push_back(convertTransaction(*it));
                _return.result = true;
                ++it;
                --limit;
            }
        }

        if (limit) {
            tPair = blockchain_.getPreviousNonEmptyBlock(tPair.first);
        }
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::TokenTransfersListGet(api::TokenTransfersResult& _return, int64_t offset, int64_t limit) {
    if (!validatePagination(_return, *this, offset, limit)) {
        return;
    }

    uint64_t totalTransfers = 0;
    std::multimap<cs::Sequence, csdb::Address> tokenTransPools;

    tm_.loadTokenInfo([&totalTransfers, &tokenTransPools, this](const TokensMap& tm_, const HoldersMap&) {
        for (auto& t : tm_) {
            totalTransfers += t.second.transfersCount;
            tokenTransPools.insert(std::make_pair(blockchain_.getLastTransaction(t.first).pool_seq(), t.first));
        }
    });

    _return.count = int32_t(totalTransfers);

    cs::Sequence seq = blockchain_.getLastNonEmptyBlock().first;
    while (limit && seq != cs::kWrongSequence && tokenTransPools.size()) {
        auto it = tokenTransPools.find(seq);
        if (it != tokenTransPools.end()) {
            auto pool = executor_.loadBlockApi(seq);

            for (auto& t : pool.transactions()) {
                if (!is_smart(t)) {
                    continue;
                }
                const auto smart = fetch_smart(t);
                if (!TokensMaster::isTransfer(smart.method, smart.params)) {
                    continue;
                }
                if (--offset >= 0) {
                    continue;
                }
                csdb::Address target_pk = blockchain_.getAddressByType(t.target(), BlockChain::AddressType::PublicKey);
                auto addrPair = TokensMaster::getTransferData(target_pk, smart.method, smart.params);
                addTokenResult(_return, target_pk, "", pool, t, smart, addrPair);
                if (--limit == 0) {
                    break;
                }
            }

            do {
                const auto lPs = blockchain_.getPreviousPoolSeq(it->second, it->first);
                const auto lAddr = it->second;

                tokenTransPools.erase(it);
                if (lPs != cs::kWrongSequence) {
                    tokenTransPools.insert(std::make_pair(lPs, lAddr));
                }

                it = tokenTransPools.find(seq);
            } while (it != tokenTransPools.end());
        }

        seq = blockchain_.getPreviousNonEmptyBlock(seq).first;
    }

    tm_.loadTokenInfo([&_return](const TokensMap& tm_, const HoldersMap&) {
        for (auto& transfer : _return.transfers) {
            if (auto it = tm_.find(BlockChain::getAddressFromKey(transfer.token)); it != tm_.end()) {
                transfer.code = it->second.symbol;
            }
        }
    });

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::TokenWalletTransfersGet(api::TokenTransfersResult& _return, const general::Address& token, const general::Address& address, int64_t offset, int64_t limit) {
    const csdb::Address wallet = BlockChain::getAddressFromKey(address);
    tokenTransactionsInternal(_return, *this, tm_, token, true, true, offset, limit, wallet);
}

void APIHandler::TokenTransactionsGet(api::TokenTransactionsResult& _return, const general::Address& token, int64_t offset, int64_t limit) {
    tokenTransactionsInternal(_return, *this, tm_, token, false, false, offset, limit);
}

void APIHandler::TokenInfoGet(api::TokenInfoResult& _return, const general::Address& token) {
    bool found = false;
    const csdb::Address addr = BlockChain::getAddressFromKey(token);
    tm_.loadTokenInfo([&token, &addr, &found, &_return](const TokensMap& tm_, const HoldersMap&) {
        auto tIt = tm_.find(addr);
        if (tIt != tm_.end()) {
            found = true;
            putTokenInfo(_return.token, token, tIt->second);
        }
    });
    SetResponseStatus(_return.status, found ? APIRequestStatusType::SUCCESS : APIRequestStatusType::FAILURE);
}

template <typename MapType, typename ComparatorType, typename FuncType>
static void applyToSortedMap(const MapType& map, const ComparatorType comparator, const FuncType func) {
    std::multiset<typename MapType::const_iterator, std::function<bool(const typename MapType::const_iterator&, const typename MapType::const_iterator&)>> s(
        [comparator](const typename MapType::const_iterator& lhs, const typename MapType::const_iterator& rhs) -> bool { return comparator(*lhs, *rhs); });

    for (auto it = map.begin(); it != map.end(); ++it) {
        s.insert(it);
    }

    for (auto& elt : s) {
        if (!func(*elt)) {
            break;
        }
    }
}

template <typename T, typename FieldType>
static std::function<bool(const T&, const T&)> getComparator(const FieldType field, const bool desc) {
    return [field, desc](const T& lhs, const T& rhs) { return desc ? (lhs.second.*field > rhs.second.*field) : (lhs.second.*field < rhs.second.*field); };
}

void APIHandler::TokenHoldersGet(api::TokenHoldersResult& _return, const general::Address& token, int64_t offset, int64_t limit, const TokenHoldersSortField order,
                                 const bool desc) {
    if (!validatePagination(_return, *this, offset, limit)) {
        return;
    }

    bool found = false;

    using HMap = decltype(Token::holders);
    using HT = HMap::value_type;

    std::function<bool(const HT&, const HT&)> comparator;

    switch (order) {
        case TH_Balance:
            comparator = [desc](const HT& lhs, const HT& rhs) { return desc ^ (stod(lhs.second.balance) < stod(rhs.second.balance)); };
            break;
        case TH_TransfersCount:
            comparator = getComparator<HT>(&Token::HolderInfo::transfersCount, desc);
            break;
    }

    const csdb::Address addr = BlockChain::getAddressFromKey(token);
    tm_.loadTokenInfo([&token, &addr, &found, &offset, &limit, &_return, comparator](const TokensMap& tm_, const HoldersMap&) {
        auto tIt = tm_.find(addr);
        if (tIt != tm_.end()) {
            found = true;
            _return.count = static_cast<int32_t>(tIt->second.realHoldersCount);

            applyToSortedMap(tIt->second.holders, comparator, [&offset, &limit, &_return, &token](const HMap::value_type& t) {
                if (TokensMaster::isZeroAmount(t.second.balance)) {
                    return true;
                }
                if (--offset >= 0) {
                    return true;
                }

                api::TokenHolder th;

                th.holder = fromByteArray(t.first.public_key());
                th.token = token;
                th.balance = t.second.balance;
                th.transfersCount = static_cast<int32_t>(t.second.transfersCount);

                _return.holders.push_back(th);

                if (--limit == 0) {
                    return false;
                }

                return true;
            });
        }
    });

    SetResponseStatus(_return.status, found ? APIRequestStatusType::SUCCESS : APIRequestStatusType::FAILURE);
}

void APIHandler::TokensListGet(api::TokensListResult& _return, int64_t offset, int64_t limit, const TokensListSortField order, const bool desc, const TokenFilters& filters) {
    if (!validatePagination(_return, *this, offset, limit)) {
        return;
    }

    using VT = TokensMap::value_type;
    std::function<bool(const VT&, const VT&)> comparator;

    switch (order) {
        case TL_Code:
            comparator = getComparator<VT>(&Token::symbol, desc);
            break;
        case TL_Name:
            comparator = getComparator<VT>(&Token::name, desc);
            break;
        case TL_TotalSupply:
            comparator = [desc](const VT& lhs, const VT& rhs) { return desc ^ (stod(lhs.second.totalSupply) < stod(rhs.second.totalSupply)); };
            break;
        case TL_Address:
            comparator = [desc](const VT& lhs, const VT& rhs) { return desc ^ (lhs.first < rhs.first); };
            break;
        case TL_HoldersCount:
            comparator = getComparator<VT>(&Token::realHoldersCount, desc);
            break;
        case TL_TransfersCount:
            comparator = getComparator<VT>(&Token::transfersCount, desc);
            break;
        case TL_TransactionsCount:
            comparator = getComparator<VT>(&Token::transactionsCount, desc);
            break;
    }

    tm_.loadTokenInfo([&, comparator](const TokensMap& tm_, const HoldersMap&) {
        _return.count = static_cast<int32_t>(tm_.size());
        applyToSortedMap(tm_, comparator, [&](const TokensMap::value_type& t) {
            if (--offset >= 0) {
                return true;
            }

            api::TokenInfo tok;
            putTokenInfo(tok, fromByteArray(t.first.public_key()), t.second);

            // filters
            auto posName = tok.name.find(filters.name);
            auto posCode = tok.code.find(filters.code);
            if ((posName != std::string::npos && posCode != std::string::npos && tok.tokenStandard == filters.tokenStandard) ||
                (posName && posCode && !tok.tokenStandard) ||
                (posName && filters.code.empty() && !tok.tokenStandard) ||
                (filters.name.empty() && posCode && tok.tokenStandard) ||
                (filters.name.empty() && posCode && !tok.tokenStandard) ||
                (filters.name.empty() && filters.code.empty() && tok.tokenStandard) ||
                (filters.name.empty() && filters.code.empty() && !tok.tokenStandard))
                _return.tokens.push_back(tok);

            if (--limit == 0)
                return false;

            return true;
            });
        });

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

//////////Wallets
#ifndef MONITOR_NODE
typedef std::list<std::pair<const cs::PublicKey*, const cs::WalletsCache::WalletData*>> WCSortedList;
template <typename T>
void walletStep(const cs::PublicKey* addr, const cs::WalletsCache::WalletData* wd, const uint64_t num,
                std::function<const T&(const cs::WalletsCache::WalletData&)> getter, std::function<bool(const T&, const T&)> comparator, WCSortedList& lst) {
    assert(num > 0);

    const T& val = getter(*wd);
    if (lst.size() < num || comparator(val, getter(*(lst.back().second)))) {
        // Guess why I can't use std::upper_bound in here
        // C++ is not as expressive as I've imagined it to be...
        auto it = lst.begin();
        while (it != lst.end() && !comparator(val, getter(*(it->second)))) {/* <-- this looks more like Lisp, doesn't it... */
            ++it;
        }

        lst.insert(it, std::make_pair(addr, wd));
        if (lst.size() > num) {
            lst.pop_back();
        }
    }
}

template <typename T>
void iterateOverWallets(std::function<const T&(const cs::WalletsCache::WalletData&)> getter, const uint64_t num, const bool desc, WCSortedList& lst, BlockChain& bc) {
    using Comparer = std::function<bool(const T&, const T&)>;
    Comparer comparator = desc ? Comparer(std::greater<T>()) : Comparer(std::less<T>());

    bc.iterateOverWallets([&lst, num, getter, comparator](const cs::PublicKey& addr, const cs::WalletsCache::WalletData& wd) {
        if (!addr.empty() && wd.balance_ >= csdb::Amount(0)) {
            walletStep(&addr, &wd, num, getter, comparator, lst);
        }
        return true;
    });
}
#endif

void APIHandler::WalletsGet(WalletsGetResult& _return, int64_t _offset, int64_t _limit, int8_t _ordCol, bool _desc) {
    if (!validatePagination(_return, *this, _offset, _limit)) {
        return;
    }

    const auto& multiWallets = blockchain_.multiWallets();
    auto order = _desc ? cs::MultiWallets::Order::Greater : cs::MultiWallets::Order::Less;
    std::vector<cs::MultiWallets::InternalData> result;

    if (_ordCol == 0) {  // Balance
        result = multiWallets.iterate<cs::MultiWallets::ByBalance>(_offset, _limit, order);
    }
    else if (_ordCol == 1) {  // Create time
#ifdef MONITOR_NODE
        result = multiWallets.iterate<cs::MultiWallets::ByCreateTime>(_offset, _limit, order);
#else
        SetResponseStatus(_return.status, APIRequestStatusType::NOT_IMPLEMENTED);
        return;
#endif
    }
    else {  // Transactions count
        result = multiWallets.iterate<cs::MultiWallets::ByTransactionsCount>(_offset, _limit, order);
    }

    for (const auto& data : result) {
        api::WalletInfo wi;

        wi.address = std::string(data.key.begin(), data.key.end());
        wi.balance.integral = data.balance.integral();
        wi.balance.fraction = static_cast<int64_t>(data.balance.fraction());
        wi.transactionsNumber = static_cast<int64_t>(data.transactionsCount);
#ifdef MONITOR_NODE
        wi.firstTransactionTime = static_cast<int64_t>(data.createTime);
#endif
        const csdb::Address addr = csdb::Address::from_public_key(data.key);
        BlockChain::WalletData wallData{};
        if (blockchain_.findWalletData(addr, wallData)) {
            auto delegated = getDelegated(wallData);
            if (delegated.has_value()) {
                wi.__set_delegated(delegated.value());
            }
        }
        _return.wallets.push_back(wi);
    }

    _return.count = static_cast<int32_t>(blockchain_.getWalletsCountWithBalance());
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::TrustedGet(TrustedGetResult& _return, int32_t _page) {
#ifdef MONITOR_NODE
    const static uint32_t PER_PAGE = 256;
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
    _page = std::max(int32_t(0), _page);

    uint32_t offset = _page * PER_PAGE;
    uint32_t limit = PER_PAGE;
    uint32_t total = 0;

    blockchain_.iterateOverWriters([&_return, &offset, &limit, &total](const cs::PublicKey& addr, const cs::WalletsCache::TrustedData& wd) {
        if (addr.empty()) {
            return true;
        }
        if (offset == 0) {
            if (limit > 0) {
                api::TrustedInfo wi;
                // const ::csdb::internal::byte_array addr_b(addr.begin(), addr.end());
                const cs::Bytes addr_b(addr.begin(), addr.end());
                wi.address = fromByteArray(addr_b);

                wi.timesWriter = uint32_t(wd.times);
                wi.timesTrusted = uint32_t(wd.times_trusted);
                wi.feeCollected.integral = wd.totalFee.integral();
                wi.feeCollected.fraction = wd.totalFee.fraction();

                _return.writers.push_back(wi);
                --limit;
            }
        }
        else {
            --offset;
        }

        ++total;
        return true;
    });
    _return.pages = (total / PER_PAGE) + (int)(total % PER_PAGE != 0);
#else
    ++_page;
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
#endif
}

void APIHandler::SyncStateGet(api::SyncStateResult& _return) {
    _return.lastBlock = static_cast<int64_t>(blockchain_.getLastSeq());
    _return.currRound = static_cast<int64_t>(cs::Conveyer::instance().currentRoundNumber());

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void apiexec::APIEXECHandler::GetSeed(apiexec::GetSeedResult& _return, const general::AccessID accessId) {
    if (accessId == cs::Executor::ACCESS_ID_RESERVE::GETTER) { // for getter
        std::default_random_engine random(std::random_device{}());
        const auto randSequence = random() % blockchain_.getLastSeq();
        const auto hash         = ::csdb::priv::crypto::calc_hash(blockchain_.getHashBySequence(randSequence).to_binary());
        _return.seed.assign(hash.begin(), hash.end());
        return;
    }
    const auto opt_sequence = executor_.getSequence(accessId);
    if (!opt_sequence.has_value()) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
        return;
    }
    const auto hash_seq = blockchain_.getHashBySequence(opt_sequence.value());
    const auto hash = ::csdb::priv::crypto::calc_hash(hash_seq.to_binary());
    std::copy(hash.begin(), hash.end(), std::inserter(_return.seed, _return.seed.end()));
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void apiexec::APIEXECHandler::SendTransaction(apiexec::SendTransactionResult& _return, const general::AccessID accessId, const api::Transaction& transaction) {
    csunused(_return);
    csunused(accessId);
    executor_.addInnerSendTransaction(accessId, executor_.makeTransaction(transaction));
}

void apiexec::APIEXECHandler::WalletIdGet(api::WalletIdGetResult& _return, const general::AccessID accessId, const general::Address& address) {
    csunused(accessId);
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::WalletData wallData{};
    BlockChain::WalletId wallId{};
    if (!blockchain_.findWalletData(addr, wallData, wallId)) {
        SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
        return;
    }
    _return.walletId = static_cast<int>(wallId);
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void apiexec::APIEXECHandler::SmartContractGet(SmartContractGetResult& _return, const general::AccessID accessId, const general::Address& address) {
    const auto addr = BlockChain::getAddressFromKey(address);
    auto opt_transaction_id = executor_.getDeployTrxn(addr);
    if (!opt_transaction_id.has_value()) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
        return;
    }

    auto trxn = executor_.loadTransactionApi(opt_transaction_id.value());
    const auto sci = cs::Serializer::deserialize<api::SmartContractInvocation>(trxn.user_field(0).value<std::string>());
    _return.byteCodeObjects = sci.smartContractDeploy.byteCodeObjects;
    const auto opt_state = executor_.getAccessState(accessId, addr);
    if (!opt_state.has_value()) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
        return;
    }
    _return.contractState = opt_state.value();
    _return.stateCanModify = (solver_.isContractLocked(addr) && executor_.isLockSmart(address, accessId)) ? true : false;

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void apiexec::APIEXECHandler::WalletBalanceGet(api::WalletBalanceGetResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::WalletData wallData{};
    if (!blockchain_.findWalletData(addr, wallData))
        return;
    _return.balance.integral = wallData.balance_.integral();
    _return.balance.fraction = static_cast<decltype(_return.balance.fraction)>(wallData.balance_.fraction());
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void apiexec::APIEXECHandler::PoolGet(PoolGetResult& _return, const int64_t sequence) {
    auto poolBin = executor_.loadBlockApi(static_cast<cs::Sequence>(sequence)).to_binary();
    _return.pool.reserve(poolBin.size());
    std::copy(poolBin.begin(), poolBin.end(), std::back_inserter(_return.pool));
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void apiexec::APIEXECHandler::GetDateTime(GetDateTimeResult& _return, const general::AccessID accessId) {
    _return.timestamp = executor_.getTimeSmartContract(accessId);
    if(_return.timestamp)
        SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
    else
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
}
