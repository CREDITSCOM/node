#include <apihandler.hpp>

#include <csnode/conveyer.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <solver/smartcontracts.hpp>
#include <src/priv_crypto.hpp>
#include "csconnector/csconnector.hpp"
#include "stdafx.h"
#include <csnode/fee.hpp>

#include <base58.h>

using namespace api;
using namespace ::apache;

inline int64_t limitPage(int64_t value) {
    return std::clamp(value, int64_t(0), int64_t(100));
}

apiexec::APIEXECHandler::APIEXECHandler(BlockChain& blockchain, cs::SolverCore& solver, executor::Executor& executor, const csconnector::Config& config)
: executor_(executor)
, blockchain_(blockchain)
, solver_(solver) {
    csunused(config);
}

APIHandler::APIHandler(BlockChain& blockchain, cs::SolverCore& _solver, executor::Executor& executor, const csconnector::Config&)
: executor_(executor)
, s_blockchain(blockchain)
, solver(_solver)
#ifdef MONITOR_NODE
, stats(blockchain)
#endif
, tm(this) {
#ifdef MONITOR_NODE
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
    if (!s_blockchain.isGood())
        return;
#ifdef MONITOR_NODE
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

/*void APIHandler::state_updater_work_function() {
    try {
        auto lasthash = s_blockchain.getLastHash();
        while (state_updater_running.test_and_set(std::memory_order_acquire)) {
            if (!update_smart_caches_once(lasthash)) {
                {
                    std::unique_lock lk(dbLock_);
                    newBlockCv_.wait(lk);
                }
                lasthash = s_blockchain.getLastHash();
            }
        }
    }
    catch (std::exception& ex) {
        std::stringstream ss;
        ss << "error [" << ex.what() << "] in file'" << __FILE__ << "' line'" << __LINE__ << "'";
        cserror() << ss.str().c_str();
    }
    catch (...) {
        std::stringstream ss;
        ss << "unknown error in file'" << __FILE__ << "' line'" << __LINE__ << "'";
        cslog() << ss.str().c_str();
    }
}*/

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
    response.code = statuses[static_cast<uint8_t>(status)].code;
    response.message = statuses[static_cast<uint8_t>(status)].message + details;
}

void APIHandlerBase::SetResponseStatus(general::APIResponse& response, bool commandWasHandled) {
    SetResponseStatus(response, (commandWasHandled ? APIRequestStatusType::SUCCESS : APIRequestStatusType::NOT_IMPLEMENTED));
}

void APIHandler::WalletDataGet(WalletDataGetResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::WalletData wallData{};
    BlockChain::WalletId wallId{};
    if (!s_blockchain.findWalletData(addr, wallData, wallId)) {
        SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
        return;
    }
    _return.walletData.walletId = wallId;
    _return.walletData.balance.integral = wallData.balance_.integral();
    _return.walletData.balance.fraction = static_cast<decltype(_return.walletData.balance.fraction)>(wallData.balance_.fraction());
    const cs::TransactionsTail& tail = wallData.trxTail_;
    _return.walletData.lastTransactionId = tail.empty() ? 0 : tail.getLastTransactionId();

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::WalletIdGet(api::WalletIdGetResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::WalletData wallData{};
    BlockChain::WalletId wallId{};
    if (!s_blockchain.findWalletData(addr, wallData, wallId)) {
        SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
        return;
    }

    _return.walletId = wallId;
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::WalletTransactionsCountGet(api::WalletTransactionsCountGetResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::WalletData wallData{};
    BlockChain::WalletId wallId{};
    if (!s_blockchain.findWalletData(addr, wallData, wallId)) {
        SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
        return;
    }
    _return.lastTransactionInnerId = wallData.trxTail_.empty() ? 0 : wallData.trxTail_.getLastTransactionId();
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::WalletBalanceGet(api::WalletBalanceGetResult& _return, const general::Address& address) {
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    BlockChain::WalletData wallData{};
    BlockChain::WalletId wallId{};
    if (!s_blockchain.findWalletData(addr, wallData, wallId)) {
        SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
        return;
    }
    _return.balance.integral = wallData.balance_.integral();
    _return.balance.fraction = static_cast<decltype(_return.balance.fraction)>(wallData.balance_.fraction());
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

std::string fromByteArray(const cs::Bytes& bar) {
    std::string res;
    {
        res.reserve(bar.size());
        std::transform(bar.begin(), bar.end(), std::back_inserter<std::string>(res), [](uint8_t _) { return char(_); });
    }
    return res;
}

std::string fromByteArray(const cs::PublicKey& bar) {
    std::string res;
    {
        res.reserve(bar.size());
        std::transform(bar.begin(), bar.end(), std::back_inserter<std::string>(res), [](uint8_t _) { return char(_); });
    }
    return res;
}

cs::Bytes toByteArray(const std::string& s) {
    cs::Bytes res;
    {
        res.reserve(s.size());
        std::transform(s.begin(), s.end(), std::back_inserter<decltype(res)>(res), [](uint8_t _) { return uint8_t(_); });
    }
    return res;
}

general::Amount convertAmount(const csdb::Amount& amount) {
	general::Amount result;
    result.integral = amount.integral();
    result.fraction = amount.fraction();
    assert(result.fraction >= 0);
    return result;
}

api::TransactionId convert_transaction_id(const csdb::TransactionID& trid) {
    api::TransactionId result_id;
    result_id.index = (uint32_t) trid.index();
    result_id.poolSeq = trid.pool_seq();
    result_id.__isset.index = true;
    result_id.__isset.poolSeq = true;
    return result_id;
}

csdb::TransactionID convert_transaction_id(const api::TransactionId& trid) {
    return csdb::TransactionID(trid.poolSeq, trid.index);
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
    return uf.type() == csdb::UserField::Type::String && is_smart_deploy(deserialize<api::SmartContractInvocation>(uf.value<std::string>()));
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
    ti.state = (api::SmartOperationState)(uint32_t)op.state;
    if (op.stateTransaction.is_valid())
        ti.__set_stateTransaction(convert_transaction_id(op.stateTransaction));
}

api::SealedTransaction APIHandler::convertTransaction(const csdb::Transaction& transaction) {
    api::SealedTransaction result;
    const csdb::Amount amount = transaction.amount();
    csdb::Currency currency = transaction.currency();
    csdb::Address address = transaction.source();
    if (address.is_wallet_id()) {
        BlockChain::WalletData data_to_fetch_pulic_key;
        s_blockchain.findWalletData(transaction.source().wallet_id(), data_to_fetch_pulic_key);
        address = csdb::Address::from_public_key(data_to_fetch_pulic_key.address_);
    }

    csdb::Address target = transaction.target();
    if (target.is_wallet_id()) {
        BlockChain::WalletData data_to_fetch_pulic_key;
        s_blockchain.findWalletData(transaction.target().wallet_id(), data_to_fetch_pulic_key);
        target = csdb::Address::from_public_key(data_to_fetch_pulic_key.address_);
    }

    result.id = convert_transaction_id(transaction.id());
    result.__isset.id = true;
    result.__isset.trxn = true;
    result.trxn.id = transaction.innerID();
    result.trxn.amount = convertAmount(amount);
    result.trxn.currency = DEFAULT_CURRENCY;
    result.trxn.source = fromByteArray(address.public_key());
    result.trxn.target = fromByteArray(target.public_key());
    result.trxn.fee.commission = transaction.counted_fee().get_raw();

    result.trxn.timeCreation = transaction.get_time();

    result.trxn.poolNumber = executor_.loadBlockApi(transaction.id().pool_seq()).sequence();

    if (is_smart(transaction)) {
        using namespace cs::trx_uf;
        auto sci = deserialize<api::SmartContractInvocation>(transaction.user_field(deploy::Code).value<std::string>());
        bool isToken = false;

        auto smartResult = getSmartStatus(transaction.id());
        result.trxn.__set_smartInfo(api::SmartTransInfo{});

        if (is_smart_deploy(sci)) {
            result.trxn.type = api::TransactionType::TT_SmartDeploy;
            tm.loadTokenInfo(std::vector<csdb::Address>(1, target), [&isToken, &target, &result](const TokensMap& tokens, const HoldersMap&) {
                auto it = tokens.find(target);
                if (it != tokens.end()) {
                    isToken = true;
                    api::TokenDeployTransInfo dti;
                    dti.name = it->second.name;
                    dti.code = it->second.symbol;
                    dti.tokenStandard = int32_t(it->second.tokenStandard);
                    result.trxn.smartInfo.__set_v_tokenDeploy(dti);
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
            result.trxn.type = api::TransactionType::TT_SmartExecute;
            if (isTransfer) {
                tm.loadTokenInfo(std::vector<csdb::Address>(1, target), [&isToken, &isTransfer, &target, &result](const TokensMap& tokens, const HoldersMap&) {
                    auto it = tokens.find(target);
                    if (it != tokens.end()) {
                        isToken = true;
                        api::TokenTransferTransInfo tti;
                        tti.code = it->second.symbol;
                        result.trxn.smartInfo.__set_v_tokenTransfer(tti);
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
        result.trxn.type = api::TransactionType::TT_SmartState;
        api::SmartStateTransInfo sti;
        sti.success = cs::SmartContracts::is_state_updated(transaction);
        sti.executionFee = convertAmount(transaction.user_field(cs::trx_uf::new_state::Fee).value<csdb::Amount>());
        cs::SmartContractRef scr;
        scr.from_user_field(transaction.user_field(cs::trx_uf::new_state::RefStart));
        sti.startTransaction = convert_transaction_id(scr.getTransactionID());

        auto fld = transaction.user_field(cs::trx_uf::new_state::RetVal);
        if (fld.is_valid()) {
            auto retVal = fld.value<std::string>();
            auto variant = deserialize<::general::Variant>(std::move(retVal));
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
        result.trxn.type = api::TransactionType::TT_Normal;
        auto ufd = transaction.user_field(1);
        if (ufd.is_valid())
            result.trxn.__set_userFields(ufd.value<std::string>());
    }

    // fill ExtraFee
    // 1) find state transaction
    csdb::Transaction stateTrx;
    if (is_smart(transaction)) {
        auto opers = lockedReference(this->smart_operations);
        auto state_id = (*opers)[transaction.id()].stateTransaction;
        if (state_id.is_valid()) {
            stateTrx = executor_.loadTransactionApi(state_id);
        }
    }
    else if (is_smart_state(transaction))
        stateTrx = transaction;

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
    for (auto trx = ++trxIt; trx != transactions.end(); ++trx) {
        if (s_blockchain.getAddressByType(trx->source(), BlockChain::AddressType::PublicKey) != 
            s_blockchain.getAddressByType(stateTrx.source(), BlockChain::AddressType::PublicKey)) // end find extra transactions                   
                break;
        extraFee.transactionId = convert_transaction_id(trx->id());
        extraFee.sum = convertAmount(csdb::Amount(trx->counted_fee().to_double()));
        extraFee.comment = "emitted trxs fee";
        result.trxn.extraFee.push_back(extraFee);
    }
    result.trxn.__isset.extraFee = true;
    return result;
}

std::vector<api::SealedTransaction> APIHandler::convertTransactions(const std::vector<csdb::Transaction>& transactions) {
    std::vector<api::SealedTransaction> result;
    auto size = transactions.size();
    result.resize(size);
    //result.resize(transactions.size());
    /*const auto convert = std::bind(&APIHandler::convertTransaction, this, std::placeholders::_1);
    std::transform(transactions.begin(), transactions.end(), result.begin(), convert);
    for (auto& it : result) {
      auto poolHash = csdb::PoolHash::from_binary(toByteArray(it.id.poolHash));
      it.trxn.timeCreation = convertPool(poolHash).time;

      auto smartResult = getSmartStatus(it.trxn.id());
      it.trxn.__set_smartInfo(api::SmartTransInfo{});
      it.trxn.smartInfo.__set_v_smartDeploy(SmartDeployTransInfo());
      fillTransInfoWithOpData(smartResult, trxn.smartInfo.v_smartDeploy);
    }*/
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
        result.poolNumber = pool.sequence();
        assert(result.poolNumber >= 0);
        result.prevHash = fromByteArray(pool.previous_hash().to_binary());
        result.time = pool.get_time();

        result.transactionsCount = (int32_t)pool.transactions_count();  // DO NOT EVER CREATE POOLS WITH
                                                                        // MORE THAN 2 BILLION
                                                                        // TRANSACTIONS, EVEN AT NIGHT

        const auto& wpk = pool.writer_public_key();
        result.writer = fromByteArray(cs::Bytes(wpk.begin(), wpk.end()));

        double totalFee = 0;
        const auto& transs = const_cast<csdb::Pool&>(pool).transactions();
        for (auto& t : transs)
            totalFee += t.counted_fee().to_double();

        const auto tf = csdb::Amount(totalFee);
        result.totalFee.integral = tf.integral();
        result.totalFee.fraction = tf.fraction();
    }
    return result;
}

api::Pool APIHandler::convertPool(const csdb::PoolHash& poolHash) {
    return convertPool(executor_.loadBlockApi(poolHash));
}

std::vector<api::SealedTransaction> APIHandler::extractTransactions(const csdb::Pool& pool, int64_t limit, const int64_t offset) {
    int64_t transactionsCount = pool.transactions_count();
    assert(transactionsCount >= 0);
    std::vector<api::SealedTransaction> result;
    if (offset > transactionsCount)
        return result;  // если запрашиваемые // транзакций выходят за // пределы пула возвращаем пустой результат
    transactionsCount -= offset;  // мы можем отдать все транзакции в пуле за вычетом смещения
    if (limit > transactionsCount)
        limit = transactionsCount;  // лимит уменьшается до реального количества // транзакций которые можно отдать
    for (int64_t index = offset; index < (offset + limit); ++index) {
        result.push_back(convertTransaction(pool.transaction(index)));
    }
    return result;
}

void APIHandler::TransactionGet(TransactionGetResult& _return, const TransactionId& transactionId) {
    const csdb::TransactionID tmpTransactionId = csdb::TransactionID(transactionId.poolSeq, (transactionId.index));
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
        s_blockchain.getTransactions(transactions, addr, static_cast<uint64_t>(offset), static_cast<uint64_t>(limit));
    }
    _return.transactions = convertTransactions(transactions);

#ifdef MONITOR_NODE
    _return.total_trxns_count = s_blockchain.getTransactionsCount(addr);
#endif

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

api::SmartContractInvocation fetch_smart(const csdb::Transaction& tr) {
    if (tr.is_valid()) {
        const auto uf = tr.user_field(cs::trx_uf::deploy::Code);
        if (uf.is_valid()) {
            std::string data = uf.value<std::string>();
            if (!data.empty()) {
                return deserialize<api::SmartContractInvocation>(std::move(data));
            }
        }
    }
    return api::SmartContractInvocation();
}

api::SmartContract APIHandler::fetch_smart_body(const csdb::Transaction& tr) {
    using namespace cs::trx_uf;
    api::SmartContract res;
    if (!tr.is_valid())
        return res;
    const auto sci = deserialize<api::SmartContractInvocation>(tr.user_field(deploy::Code).value<std::string>());
    res.smartContractDeploy.byteCodeObjects = sci.smartContractDeploy.byteCodeObjects;
    res.smartContractDeploy.sourceCode = sci.smartContractDeploy.sourceCode;
    res.smartContractDeploy.hashState = sci.smartContractDeploy.hashState;
    res.deployer = fromByteArray(s_blockchain.getAddressByType(tr.source(), BlockChain::AddressType::PublicKey).public_key());
    res.address = fromByteArray(s_blockchain.getAddressByType(tr.target(), BlockChain::AddressType::PublicKey).public_key());

#ifdef TOKENS_CACHE
    tm.loadTokenInfo(std::vector<csdb::Address>(1, tr.target()), [&tr, &res](const TokensMap& tokens, const HoldersMap&) {
        auto it = tokens.find(tr.target());
        if (it != tokens.end())
            res.smartContractDeploy.tokenStandard = it->second.tokenStandard;
        else
            res.smartContractDeploy.tokenStandard = TokenStandard::NotAToken;
    });
#else
    res.smartContractDeploy.tokenStandard = TokenStandard::NotAToken;
#endif

#ifdef MONITOR_NODE
    s_blockchain.applyToWallet(tr.target(), [&res](const cs::WalletsCache::WalletData& wd) { res.createTime = wd.createTime_; });
#endif
	if (tr.user_field(0).is_valid())
		res.transactionsCount = s_blockchain.getTransactionsCount(tr.target());

    auto pool = executor_.loadBlockApi(tr.id().pool_seq());
    res.createTime = pool.get_time();

    return res;
}

template <typename T>
auto set_max_fee(T& trx, const csdb::Amount& am, int) -> decltype(trx.set_max_fee(am), void()) {
    trx.set_max_fee(am);
}

template <typename T>
void set_max_fee(T&, const csdb::Amount&, long) {
}

csdb::Transaction APIHandler::make_transaction(const Transaction& transaction) {
    csdb::Transaction send_transaction;
    const auto source = BlockChain::getAddressFromKey(transaction.source);
    const uint64_t WALLET_DENOM = csdb::Amount::AMOUNT_MAX_FRACTION;  // 1'000'000'000'000'000'000ull;
    send_transaction.set_amount(csdb::Amount(transaction.amount.integral, transaction.amount.fraction, WALLET_DENOM));
    BlockChain::WalletData wallData{};
    BlockChain::WalletId id{};

    if (transaction.__isset.smartContract && !transaction.smartContract.forgetNewState &&  // not for getter
            !s_blockchain.findWalletData(source, wallData, id)) {
        return csdb::Transaction{};
    }

    send_transaction.set_currency(csdb::Currency(1));
    send_transaction.set_source(source);
    send_transaction.set_target(BlockChain::getAddressFromKey(transaction.target));
    send_transaction.set_max_fee(csdb::AmountCommission((uint16_t)transaction.fee.commission));
    send_transaction.set_innerID(transaction.id & 0x3fffffffffff);

    // TODO Change Thrift to avoid copy
    cs::Signature signature{};

    if (transaction.signature.size() == signature.size()) {
        std::copy(transaction.signature.begin(), transaction.signature.end(), signature.begin());
    }

    send_transaction.set_signature(signature);
    return send_transaction;
}

std::string get_delimited_transaction_sighex(const csdb::Transaction& tr) {
    auto bs = fromByteArray(tr.to_byte_stream_for_sig());
    return std::string({' '}) + cs::Utils::byteStreamToHex(bs.data(), bs.length());
}

void APIHandler::dumb_transaction_flow(api::TransactionFlowResult& _return, const Transaction& transaction) {
    auto tr = make_transaction(transaction);
    if (!transaction.userFields.empty())
        tr.add_user_field(cs::trx_uf::ordinary::Text, transaction.userFields);
    solver.send_wallet_transaction(tr);
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS, get_delimited_transaction_sighex(tr));
}

template <typename T>
std::enable_if<std::is_convertible<T*, ::apache::thrift::TBase*>::type, std::ostream&> operator<<(std::ostream& s, const T& t) {
    t.printTo(s);
    return s;
}

std::optional<std::string> APIHandler::checkTransaction(const Transaction& transaction) {
    if (transaction.__isset.smartContract && transaction.smartContract.forgetNewState)
        return {};

    auto trxn = make_transaction(transaction);
    if (transaction.__isset.smartContract)
        trxn.add_user_field(cs::trx_uf::deploy::Code, serialize(transaction.smartContract));
    else if(!transaction.userFields.empty())
        trxn.add_user_field(cs::trx_uf::ordinary::Text, transaction.userFields);

    // for payable
    if (transaction.__isset.usedContracts && !transaction.usedContracts.empty() && !transaction.__isset.smartContract) {
        std::string uf;
        for (auto& addr : transaction.usedContracts) {
            uf += addr;
        }
        trxn.add_user_field(cs::trx_uf::ordinary::UsedContracts, uf);
    }

    // check money
    const auto source_addr = s_blockchain.getAddressByType(trxn.source(), BlockChain::AddressType::PublicKey);
    BlockChain::WalletData wallData{};
    BlockChain::WalletId wallId{};
    if (!s_blockchain.findWalletData(source_addr, wallData, wallId))
        return "not enough money!";

    const auto max_fee = trxn.max_fee().to_double();
    const auto balance = wallData.balance_.to_double();
    if (max_fee > balance)
        return "not enough money!\nmax_fee: " + std::to_string(max_fee) + "\nbalance: " + std::to_string(balance);

    // check max fee
    csdb::AmountCommission countedFee;
    if (!cs::fee::estimateMaxFee(trxn, countedFee))
        return "max fee is not enough, counted fee will be " + std::to_string(countedFee.to_double());

    // check signature
    const auto byteStream = trxn.to_byte_stream_for_sig();
    if (!cscrypto::verifySignature(trxn.signature(), s_blockchain.getAddressByType(trxn.source(), BlockChain::AddressType::PublicKey).public_key(),
        byteStream.data(), byteStream.size())) {
        cslog() << "API: reject transaction with wrong signature";
        return "wrong signature! ByteStream: " + cs::Utils::byteStreamToHex(fromByteArray(byteStream));
    }
    return {};
}

void APIHandler::smart_transaction_flow(api::TransactionFlowResult& _return, const Transaction& transaction) {
    auto input_smart        = transaction.__isset.smartContract ? transaction.smartContract : SmartContractInvocation{};
    auto send_transaction   = make_transaction(transaction);
    const auto smart_addr   = s_blockchain.getAddressByType(send_transaction.target(), BlockChain::AddressType::PublicKey);
    bool deploy             = transaction.__isset.smartContract ? is_smart_deploy(input_smart) : false;

    if (transaction.__isset.smartContract) {
        send_transaction.add_user_field(cs::trx_uf::deploy::Code, serialize(transaction.smartContract));
    } else if (!transaction.userFields.empty()) { // for payable
        send_transaction.add_user_field(cs::trx_uf::ordinary::Text, transaction.userFields);
        deploy = false;
    }

    std::vector<general::ByteCodeObject> origin_bytecode;
    if (!deploy) {
        for (auto& it : input_smart.smartContractDeploy.byteCodeObjects) {
            it.byteCode.clear();
        }
        input_smart.smartContractDeploy.sourceCode.clear();
        decltype(auto) locked_smart_origin = lockedReference(this->smart_origin);
        auto it = locked_smart_origin->find(smart_addr);
        if (it != locked_smart_origin->end())
            origin_bytecode = fetch_smart(executor_.loadTransactionApi(it->second)).smartContractDeploy.byteCodeObjects;
        else {
            SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
            return;
        }
    }
    else {
        csdb::Address addr = s_blockchain.getAddressByType(send_transaction.target(), BlockChain::AddressType::PublicKey);
        csdb::Address deployer = s_blockchain.getAddressByType(send_transaction.source(), BlockChain::AddressType::PublicKey);
        auto scKey = cs::SmartContracts::get_valid_smart_address(deployer, send_transaction.innerID(), input_smart.smartContractDeploy);
        if (scKey != addr) {
            _return.status.code = ERROR_CODE;
            const auto data = scKey.public_key().data();
            std::string str = EncodeBase58(data, data + cscrypto::kPublicKeySize);
            _return.status.message = "Bad smart contract address, expected " + str;
            return;
        }
    }

    auto& hashStateEntry = [this, &smart_addr]() -> decltype(auto) {
        auto hashStateInst(lockedReference(this->hashStateSL));
        return (*hashStateInst)[smart_addr];
    }();

    hashStateEntry.getPosition();

    if (input_smart.forgetNewState) {
        auto source_pk = s_blockchain.getAddressByType(send_transaction.source(), BlockChain::AddressType::PublicKey).to_api_addr();
        auto target_pk = s_blockchain.getAddressByType(send_transaction.target(), BlockChain::AddressType::PublicKey).to_api_addr();
        executor::ExecuteByteCodeResult api_resp;
        const std::vector<general::ByteCodeObject>& bytecode = deploy ? input_smart.smartContractDeploy.byteCodeObjects : origin_bytecode;
        if (!deploy || !input_smart.smartContractDeploy.byteCodeObjects.empty()) {
            std::vector<executor::MethodHeader> methodHeader;
            {
                executor::MethodHeader tmp;
                tmp.methodName  = input_smart.method;
                tmp.params      = input_smart.params;
                methodHeader.push_back(tmp);
            }
            auto smartAddr = s_blockchain.getAddressByType(send_transaction.target(), BlockChain::AddressType::PublicKey);
            std::string contract_state = cs::SmartContracts::get_contract_state(s_blockchain, smartAddr);

            executor_.executeByteCode(api_resp, source_pk, target_pk, bytecode, contract_state, methodHeader, true, executor::Executor::kUseLastSequence);
            if (api_resp.status.code) {
                _return.status.code = api_resp.status.code;
                _return.status.message = api_resp.status.message;
                hashStateEntry.yield();
                return;
            }
            _return.__isset.smart_contract_result = api_resp.__isset.results;
            if (_return.__isset.smart_contract_result && !api_resp.results.empty())
                _return.__set_smart_contract_result(api_resp.results[0].ret_val);
        }

        SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
        hashStateEntry.yield();
        return;
    }

    solver.send_wallet_transaction(send_transaction);

    cs::Hash hashState;
    if (deploy) {        
        auto resWait = hashStateEntry.waitTillFront([&](HashState& ss) {
            hashState = ss.hash;
            if (!ss.condFlg)
                return false;
            ss.condFlg = false;         
            return true;
        });
        if (!resWait) {  // time is over
            SetResponseStatus(_return.status, APIRequestStatusType::INPROGRESS);
            return;
        }
        if(hashState == cs::Zero::hash) {
            _return.status.code = ERROR_CODE;
            _return.status.message = "new hash of state is empty!";
            return;
        }     
    }
    else {
        std::string retVal;
        auto resWait = hashStateEntry.waitTillFront([&](HashState& ss) {
            if(!ss.condFlg)
                return false;
            hashState   = ss.hash;
            retVal      = ss.retVal;
            ss.condFlg  = false;
            return true;
        });

        if (!resWait) { // time is over
            SetResponseStatus(_return.status, APIRequestStatusType::INPROGRESS);
            return;
        }
        if (hashState.empty()) {
            _return.status.code = ERROR_CODE;
            _return.status.message = "new hash of state is empty!";
            return;
        }
        if (!retVal.empty())
            _return.__set_smart_contract_result(deserialize<::general::Variant>(std::move(retVal)));
    }
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS, get_delimited_transaction_sighex(send_transaction));
}

void APIHandler::TransactionFlow(api::TransactionFlowResult& _return, const Transaction& transaction) {
    _return.roundNum = (uint32_t)cs::Conveyer::instance().currentRoundTable().round;

    if (auto errInfo = checkTransaction(transaction); errInfo.has_value()) {
        _return.status.code     = ERROR_CODE;
        _return.status.message  = errInfo.value();
        return;
    }

    if(!transaction.__isset.smartContract && !solver.smart_contracts().is_known_smart_contract(BlockChain::getAddressFromKey(transaction.target)))
        dumb_transaction_flow(_return, transaction);
    else
        smart_transaction_flow(_return, transaction); 
}

void APIHandler::PoolListGet(api::PoolListGetResult& _return, const int64_t offset, const int64_t const_limit) {
    cs::Sequence limit = limitPage(const_limit);

    uint64_t sequence = s_blockchain.getLastSeq();
    if ((uint64_t)offset > sequence) {
        return;
    }

    _return.pools.reserve(limit);

    PoolListGetStable(_return, sequence - offset, limit);
    _return.count = uint32_t(sequence + 1);
}

void APIHandler::PoolTransactionsGet(PoolTransactionsGetResult& _return, const int64_t sequence, const int64_t offset, const int64_t const_limit) {
    //if (!validatePagination(_return, *this, offset, const_limit)) {
    //    return;
    //}
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
#ifdef MONITOR_NODE
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
#endif
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
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
    auto transaction = solver.smart_contracts().get_contract_deploy(adrs);
    if (!transaction.is_valid()) {
        transaction = executor_.loadTransactionApi(smartrid);
    }
    _return.smartContract = fetch_smart_body(transaction);
    _return.smartContract.objectState = cs::SmartContracts::get_contract_state(s_blockchain, adrs);

    SetResponseStatus(_return.status, !_return.smartContract.address.empty() ? APIRequestStatusType::SUCCESS : APIRequestStatusType::FAILURE);
    return;
}

void APIHandler::store_block_slot(const csdb::Pool& pool) {
    updateSmartCachesPool(pool);
}

void APIHandler::collect_all_stats_slot(const csdb::Pool& pool) {
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
        periodStats.transactionsCount += (uint32_t)transactionsCount;

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

            periodStats.balancePerCurrency[currency].integral += amount.integral();
            periodStats.balancePerCurrency[currency].fraction += amount.fraction();
        }
    }
    else if ((currentCutIndex + 1) < stats_.first.size()) {
        startCutTime = stats_.first[currentCutIndex].timeStamp;
        endCutTime = stats_.first[currentCutIndex + 1].timeStamp;
        ++currentCutIndex;
    }

    auto period = (csstats::Period)poolAgeSec.count();
    for (size_t periodIndex = 0; periodIndex < periods.size(); ++periodIndex) {
        if (period < periods[periodIndex]) {
            csstats::PeriodStats& periodStats = stats_.second[periodIndex];
            periodStats.poolsCount++;

            size_t transactionsCount = pool.transactions_count();
            periodStats.transactionsCount += (uint32_t)transactionsCount;

            for (size_t i = 0; i < transactionsCount; ++i) {
                const auto& transaction = pool.transaction(csdb::TransactionID(pool.sequence(), i));

                if (transaction.source() == s_blockchain.getGenesisAddress())
                    continue;
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

                periodStats.balancePerCurrency[currency].integral += amount.integral();
                periodStats.balancePerCurrency[currency].fraction += amount.fraction();
            }
        }
    }
}
//

bool APIHandler::updateSmartCachesTransaction(csdb::Transaction trxn, cs::Sequence sequence) {
    auto source_pk = s_blockchain.getAddressByType(trxn.source(), BlockChain::AddressType::PublicKey);
    auto target_pk = s_blockchain.getAddressByType(trxn.target(), BlockChain::AddressType::PublicKey);

    if (is_smart_state(trxn)) {
        cs::SmartContractRef scr(trxn.user_field(cs::trx_uf::new_state::RefStart));
        csdb::TransactionID trId(scr.sequence, scr.transaction);
        const auto execTrans = solver.smart_contracts().get_contract_call(trxn);

        if ((execTrans.is_valid() && is_smart(execTrans)) ||
            execTrans.amount().to_double()) { // payable
            const auto smart = fetch_smart(execTrans);

            if (!smart.method.empty())
                mExecuteCount_[smart.method]++;

            {
                csdb::UserField uf = trxn.user_field(cs::trx_uf::new_state::RetVal);
                std::string retVal = uf.value<std::string>();
                ::general::Variant val;
                if (!retVal.empty())
                    val = deserialize<::general::Variant>(std::move(retVal));

                auto opers = lockedReference(this->smart_operations);
                auto& op = (*opers)[trId];
                op.state = cs::SmartContracts::is_state_updated(trxn) ? SmartOperation::State::Success : SmartOperation::State::Failed;
                op.stateTransaction = trxn.id();

                if (!retVal.empty()) {
                    op.hasRetval = true;
                    if (val.__isset.v_boolean || val.__isset.v_boolean_box) {
                        op.returnsBool = true;
                        op.boolResult = val.__isset.v_boolean ? val.v_boolean : val.v_boolean_box;
                    }
                }
            }

            cs::Hash newHash;
            { // signal to end waiting for a transaction
                auto hashStateInst(lockedReference(this->hashStateSL));
                (*hashStateInst)[target_pk].updateHash([&](const HashState& oldHash) {
                    HashState res;
                    
                    auto newHashStr = trxn.user_field(cs::trx_uf::new_state::Hash).template value<std::string>();    
                    if (!newHashStr.empty())
                        std::copy(newHashStr.begin(), newHashStr.end(), res.hash.begin());
                    res.retVal = trxn.user_field(cs::trx_uf::new_state::RetVal).template value<std::string>();                                    
                    res.isOld   = (newHash == oldHash.hash);
                    res.condFlg = true;
                    return res;
                    });
            }

            if (newHash != cs::Zero::hash) { // update tokens
                auto caller_pk = s_blockchain.getAddressByType(execTrans.source(), BlockChain::AddressType::PublicKey);

                if (is_smart_deploy(smart))
                    tm.checkNewDeploy(target_pk, caller_pk, smart);

                // state also will be updated in update_smart_state_slot()
                std::string newState = cs::SmartContracts::get_contract_state(s_blockchain, target_pk);
                if (!newState.empty())
                    tm.checkNewState(target_pk, caller_pk, smart, newState);
            }
        }
    }
    else {
        {
            auto& e = [&]() -> decltype(auto) {
                auto smart_last_trxn = lockedReference(this->smart_last_trxn);
                return (*smart_last_trxn)[target_pk];
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
                auto locked_smart_origin = lockedReference(this->smart_origin);
                (*locked_smart_origin)[target_pk] = trxn.id().clone();
                executor_.updateDeployTrxns(target_pk, trxn.id().clone());
            }
            auto locked_deployed_by_creator = lockedReference(this->deployed_by_creator);
            (*locked_deployed_by_creator)[source_pk].push_back(trxn.id().clone());
        }
        return true;
    }
    return false;
}

void APIHandler::updateSmartCachesPool(const csdb::Pool& pool) {
    if (!pool.is_valid() || !pool.transactions_count())
        return;

    for (auto& trx : pool.transactions()) {
        if (is_smart(trx) || is_smart_state(trx))
            updateSmartCachesTransaction(trx, pool.sequence());
    }
}

template <typename Mapper>
size_t APIHandler::getMappedDeployerSmart(const csdb::Address& deployer, Mapper mapper, std::vector<decltype(mapper(api::SmartContract()))>& out) {
    auto locked_deployed_by_creator = lockedReference(this->deployed_by_creator);
    auto& elt = (*locked_deployed_by_creator)[deployer];
    for (auto& trid : elt) {
        auto tr = executor_.loadTransactionApi(trid);
        auto smart = fetch_smart_body(tr);
        out.push_back(mapper(smart));
    }

    return elt.size();
}

void APIHandler::SmartContractsListGet(api::SmartContractsListGetResult& _return, const general::Address& deployer) {
    const csdb::Address addr = BlockChain::getAddressFromKey(deployer);

    _return.count = static_cast<decltype(_return.count)>(getMappedDeployerSmart(addr, [](const api::SmartContract& smart) {
        return smart;
    }, _return.smartContractsList));

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::SmartContractAddressesListGet(api::SmartContractAddressesListGetResult& _return, const general::Address& deployer) {
    const csdb::Address addr = BlockChain::getAddressFromKey(deployer);

    getMappedDeployerSmart(addr, [](const SmartContract& sc) {
        return sc.address;
    }, _return.addressesList);

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::GetLastHash(api::PoolHash& _return) {
    _return = fromByteArray(s_blockchain.getLastHash().to_binary());
    return;
}

void APIHandler::PoolListGetStable(api::PoolListGetResult& _return, const int64_t sequence, const int64_t const_limit) {
    auto limit = limitPage(const_limit);
    cs::Sequence seq = cs::Sequence(sequence);
    if (seq < 0) {
        return;
    }
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
                    _return.count = uint32_t(seq + 1);
                    limSet = true;
                }
            }
        }
        else {
            _return.pools.push_back(cch->second);
            if (!limSet) {
                _return.count = uint32_t(cch->second.poolNumber + 1);
                limSet = true;
            }
        }

        --seq;
        --limit;
    }
}

void APIHandler::WaitForSmartTransaction(api::TransactionId& _return, const general::Address& smart_public) {
    csdb::Address key = BlockChain::getAddressFromKey(smart_public);
    decltype(smart_last_trxn)::LockedType::iterator it;
    auto& entry = [&]() -> decltype(auto) {
        auto smart_last_trxn = lockedReference(this->smart_last_trxn);
        std::tie(it, std::ignore) = smart_last_trxn->emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple());
        return std::ref(it->second).get();
    }();

    {
        std::unique_lock<decltype(entry.lock)> l(entry.lock);
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
        entry.new_trxn_cv.wait(l, checker);
    }
}

void APIHandler::SmartContractsAllListGet(SmartContractsListGetResult& _return, const int64_t _offset, const int64_t _limit) {
    int64_t offset = _offset;
    int64_t limit = limitPage(_limit);

    auto locked_smart_origin = lockedReference(this->smart_origin);

    _return.count = (uint32_t) locked_smart_origin->size();

    for (auto p : *locked_smart_origin) {
        if (offset) {
            --offset;
        }
        else if (limit) {
            auto tr = solver.smart_contracts().get_contract_deploy(p.first);
            if (!tr.is_valid()) {
                auto trid = p.second;
                tr = executor_.loadTransactionApi(trid);
            }
            _return.smartContractsList.push_back(fetch_smart_body(tr));
            --limit;
        }
        else {
            break;
        }
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void api::APIHandler::WaitForBlock(PoolHash& _return, const PoolHash& /* obsolete */) {
    std::unique_lock lock(dbLock_);
    newBlockCv_.wait(lock);
    _return = fromByteArray(s_blockchain.getLastHash().to_binary());
}

void APIHandler::TransactionsStateGet(TransactionsStateGetResult& _return, const general::Address& address, const std::vector<int64_t>& v) {
    csunused(v);
    csunused(address);
#if 0
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    for (auto inner_id : v) {
        csdb::Transaction transactionTmp;
        BlockChain::WalletData wallData{};
        BlockChain::WalletId wallId{};
        inner_id &= 0x3fffffffffff;
        bool finish_for_idx = false;
        if (!s_blockchain.findWalletData(addr, wallData, wallId)) {
            SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
            return;
        }
        auto addr_id = csdb::Address::from_wallet_id(wallId);
        if (s_blockchain.getTransaction(addr_id, inner_id, transactionTmp)) {// find in blockchain
            _return.states[inner_id] = VALID;
        }
        else {
            cs::Conveyer& conveyer = cs::Conveyer::instance();
            auto lock = conveyer.lock();
            for (decltype(auto) it : conveyer.packetQueue()) {
                const auto& transactions = it.transactions();
                for (decltype(auto) transaction : transactions) {
                    if (transaction.innerID() == inner_id) {
                        _return.states[inner_id] = INPROGRESS;
                        finish_for_idx = true;
                        break;
                    }
                }
            }
            if (!finish_for_idx) {
                decltype(auto) m_hash_tb = conveyer.transactionsPacketTable();  // find in hash table
                for (decltype(auto) it : m_hash_tb) {
                    const auto& transactions = it.second.transactions();
                    for (decltype(auto) transaction : transactions) {
                        if (transaction.innerID() == inner_id) {
                            _return.states[inner_id] = INPROGRESS;
                            finish_for_idx = true;
                            break;
                        }
                    }
                }
            }
            lock.unlock();
            if (!finish_for_idx) {                                // if hash table doesn't contain trx return true if in last 5 rounds
                if (conveyer.isMetaTransactionInvalid(inner_id)) {// trx is invalid (time between del from hash table and add to blockchain)
                    _return.states[inner_id] = INVALID;
                }
                else {
                    _return.states[inner_id] = VALID;
                }
            }
        }
    }
#endif // 0
    _return.roundNum = (uint32_t) cs::Conveyer::instance().currentRoundTable().round;
    SetResponseStatus(_return.status, APIRequestStatusType::NOT_IMPLEMENTED);
}

void api::APIHandler::SmartMethodParamsGet(SmartMethodParamsGetResult& _return, const general::Address& address, const int64_t id) {
    csunused(id);
    csunused(address);
#if 0
    csdb::Transaction trx;
    const csdb::Address addr = BlockChain::getAddressFromKey(address);
    if (!s_blockchain.getTransaction(addr, id, trx)) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
        return;
    }
    _return.method = convertTransaction(trx).trxn.smartContract.method;
    _return.params = convertTransaction(trx).trxn.smartContract.params;
#endif // 0
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

    for (size_t Count = 0; Count < executor_ret.methods.size(); Count++) {
        _return.methods[Count].name = executor_ret.methods[Count].name;

        for (size_t SubCount = 0; SubCount < executor_ret.methods[Count].arguments.size(); SubCount++) {
            _return.methods[Count].arguments[SubCount].type = executor_ret.methods[Count].arguments[SubCount].type;
            _return.methods[Count].arguments[SubCount].name = executor_ret.methods[Count].arguments[SubCount].name;
        }

        _return.methods[Count].returnType = executor_ret.methods[Count].returnType;
    }
}

////////new
void addTokenResult(api::TokenTransfersResult& _return, const csdb::Address& token, const std::string& code, const csdb::Pool& pool, const csdb::Transaction& tr,
                    const api::SmartContractInvocation& smart, const std::pair<csdb::Address, csdb::Address>& addrPair, const BlockChain& handler) {
    api::TokenTransfer transfer;
    transfer.token = fromByteArray(token.public_key());
    transfer.code = code;
    transfer.sender = fromByteArray(addrPair.first.public_key());
    transfer.receiver = fromByteArray(addrPair.second.public_key());
    transfer.amount = TokensMaster::getAmount(smart);
    transfer.initiator = fromByteArray(handler.getAddressByType(tr.source(), BlockChain::AddressType::PublicKey).public_key());

    transfer.transaction.poolSeq = tr.id().pool_seq();
    transfer.transaction.index = (uint32_t) tr.id().index();
    transfer.time = atoll(pool.user_field(0).value<std::string>().c_str());
    _return.transfers.push_back(transfer);
}

void addTokenResult(api::TokenTransactionsResult& _return, const csdb::Address& token, const std::string&, const csdb::Pool& pool, const csdb::Transaction& tr,
                    const api::SmartContractInvocation& smart, const std::pair<csdb::Address, csdb::Address>&, BlockChain& handler) {
    api::TokenTransaction trans;
    trans.token = fromByteArray(token.public_key());
    trans.transaction.poolSeq = tr.id().pool_seq();
    trans.transaction.index = (uint32_t) tr.id().index();
    trans.time = atoll(pool.user_field(0).value<std::string>().c_str());
    trans.initiator = fromByteArray(handler.getAddressByType(tr.source(), BlockChain::AddressType::PublicKey).public_key());
    trans.method = smart.method;
    trans.params = smart.params;
    _return.transactions.push_back(trans);
}

void putTokenInfo(api::TokenInfo& ti, const general::Address& addr, const Token& token) {
    ti.address           = addr;
    ti.code              = token.symbol;
    ti.name              = token.name;
    ti.totalSupply       = token.totalSupply;
    ti.owner = fromByteArray(token.owner.public_key());
    ti.transfersCount    = (uint32_t) token.transfersCount;
    ti.transactionsCount = (uint32_t) token.transactionsCount;
    ti.holdersCount      = (uint32_t) token.realHoldersCount;
    ti.tokenStandard     = (decltype(api::TokenInfo::tokenStandard))(uint32_t)(token.tokenStandard);
}

template <typename ResultType>
void tokenTransactionsInternal(ResultType& _return, APIHandler& handler, TokensMaster& tm, const general::Address& token, bool transfersOnly, bool filterByWallet, int64_t offset,
                               int64_t limit, const csdb::Address& wallet = csdb::Address()) {
    if (!validatePagination(_return, handler, offset, limit)) {
        return;
    }

    const csdb::Address addr = BlockChain::getAddressFromKey(token);
    bool tokenFound = false;
    std::string code;

    tm.loadTokenInfo(std::vector<csdb::Address>(1, addr), [&addr, &tokenFound, &transfersOnly, &filterByWallet, &code, &wallet, &_return](const TokensMap& tm, const HoldersMap&) {
        auto it = tm.find(addr);
        tokenFound = !(it == tm.end());
        if (tokenFound) {
            code = it->second.symbol;
            if (transfersOnly && !filterByWallet) {
                _return.count = it->second.transfersCount;
            }
            else if (!transfersOnly) {
                _return.count = it->second.transactionsCount;
            }
            else if (transfersOnly && filterByWallet) {
                auto hIt = it->second.holders.find(wallet);
                if (hIt != it->second.holders.end()) {
                    _return.count = hIt->second.transfersCount;
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

    handler.iterateOverTokenTransactions(addr, [&_return, &offset, &limit, &addr, &code, &transfersOnly, &filterByWallet, &wallet, &s_blockchain = handler.get_s_blockchain()](
                                                   const csdb::Pool& pool, const csdb::Transaction& tr) {
        auto smart = fetch_smart(tr);
        if (transfersOnly && !TokensMaster::isTransfer(smart.method, smart.params)) {
            return true;
        }

        csdb::Address addr_pk = s_blockchain.getAddressByType(tr.source(), BlockChain::AddressType::PublicKey);
        auto addrPair = TokensMaster::getTransferData(addr_pk, smart.method, smart.params);

        if (filterByWallet && addrPair.first != wallet && addrPair.second != wallet) {
            return true;
        }

        if (--offset >= 0) {
            return true;
        }

        addTokenResult(_return, addr, code, pool, tr, smart, addrPair, s_blockchain);
        return !(--limit == 0);
    });

    handler.SetResponseStatus(_return.status, APIHandlerBase::APIRequestStatusType::SUCCESS);
}

void APIHandler::iterateOverTokenTransactions(const csdb::Address& addr, const std::function<bool(const csdb::Pool&, const csdb::Transaction&)> func) {
    std::list<csdb::TransactionID> l_id;
    for (auto trIt = TransactionsIterator(s_blockchain, addr); trIt.isValid(); trIt.next()) {
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

////////new
api::SmartContractInvocation APIHandler::getSmartContract(const csdb::Address& addr, bool& present) {
    csdb::Address abs_addr = addr;
    if (addr.is_wallet_id()) {
        abs_addr = s_blockchain.getAddressByType(addr, BlockChain::AddressType::PublicKey);
    }

    const auto deploy = solver.smart_contracts().get_contract_deploy(addr);
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
    std::string state = cs::SmartContracts::get_contract_state(s_blockchain, addr);

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
    tm.loadTokenInfo(std::vector(1, addr), [&_return, &addr](const TokensMap& tokens, const HoldersMap& holders) {
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
    tokenTransactionsInternal(_return, *this, tm, token, true, false, offset, limit);
}

void APIHandler::TokenTransferGet(api::TokenTransfersResult& _return, const general::Address& token, const TransactionId& id) {
    const csdb::TransactionID trxn_id = csdb::TransactionID(id.poolSeq, id.index);
    const csdb::Transaction trxn = executor_.loadTransactionApi(trxn_id);
    const csdb::Address addr = BlockChain::getAddressFromKey(token);

    std::string code{};
    tm.loadTokenInfo(std::vector(1, addr), [&addr, &code](const TokensMap& tm, const HoldersMap&) {
        const auto it = tm.find(addr);
        if (it != tm.cend()) {
            code = it->second.symbol;
        }
    });

    if (code.empty()) {
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
        return;
    }

    const auto pool = executor_.loadBlockApi(trxn.id().pool_seq());
    const auto smart = fetch_smart(trxn);
    const auto addr_pk = s_blockchain.getAddressByType(trxn.source(), BlockChain::AddressType::PublicKey);
    const auto addrPair = TokensMaster::getTransferData(addr_pk, smart.method, smart.params);

    _return.count = 1;

    addTokenResult(_return, addr, code, pool, trxn, smart, addrPair, s_blockchain);
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::TransactionsListGet(api::TransactionsGetResult& _return, int64_t offset, int64_t limit) {
    if (!validatePagination(_return, *this, offset, limit))
        return;

    _return.result = false;
    _return.total_trxns_count = (uint32_t) s_blockchain.getTransactionsCount();

    auto tPair = s_blockchain.getLastNonEmptyBlock();
    while (limit > 0 && tPair.second) {
        if (tPair.second <= offset)
            offset -= tPair.second;
        else {
            auto p = executor_.loadBlockApi(tPair.first);
            auto it = p.transactions().rbegin() + offset;
            offset = 0;

            while (it != p.transactions().rend() && limit > 0) {
                it->set_time(p.get_time());
                _return.transactions.push_back(convertTransaction(*it));
                _return.result = true;
                ++it;
                --limit;
            }
        }

        if (limit) {
            tPair = s_blockchain.getPreviousNonEmptyBlock(tPair.first);
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

    tm.loadTokenInfo({}, [&totalTransfers, &tokenTransPools, this](const TokensMap& tm, const HoldersMap&) {
        for (auto& t : tm) {
            totalTransfers += t.second.transfersCount;
            tokenTransPools.insert(std::make_pair(s_blockchain.getLastTransaction(t.first).pool_seq(), t.first));
        }
    }, false);

    _return.count = uint32_t(totalTransfers);

    std::vector<csdb::Address> tokenAddrs;
    cs::Sequence seq = s_blockchain.getLastNonEmptyBlock().first;
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
                csdb::Address target_pk = s_blockchain.getAddressByType(t.target(), BlockChain::AddressType::PublicKey);
                auto addrPair = TokensMaster::getTransferData(target_pk, smart.method, smart.params);
                addTokenResult(_return, target_pk, "", pool, t, smart, addrPair, s_blockchain);
                tokenAddrs.push_back(target_pk);
                if (--limit == 0) {
                    break;
                }
            }

            do {
                const auto lPs = s_blockchain.getPreviousPoolSeq(it->second, it->first);
                const auto lAddr = it->second;

                tokenTransPools.erase(it);
                if (lPs != cs::kWrongSequence) {
                    tokenTransPools.insert(std::make_pair(lPs, lAddr));
                }

                it = tokenTransPools.find(seq);
            } while (it != tokenTransPools.end());
        }

        seq = s_blockchain.getPreviousNonEmptyBlock(seq).first;
    }

    tm.loadTokenInfo(tokenAddrs, [&_return](const TokensMap& tm, const HoldersMap&) {
        for (auto& transfer : _return.transfers) {
            if(auto it = tm.find(BlockChain::getAddressFromKey(transfer.token)); it != tm.end())
                transfer.code = it->second.symbol;
        }
    });

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void APIHandler::TokenWalletTransfersGet(api::TokenTransfersResult& _return, const general::Address& token, const general::Address& address, int64_t offset, int64_t limit) {
    const csdb::Address wallet = BlockChain::getAddressFromKey(address);
    tokenTransactionsInternal(_return, *this, tm, token, true, true, offset, limit, wallet);
}

void APIHandler::TokenTransactionsGet(api::TokenTransactionsResult& _return, const general::Address& token, int64_t offset, int64_t limit) {
    tokenTransactionsInternal(_return, *this, tm, token, false, false, offset, limit);
}

void APIHandler::TokenInfoGet(api::TokenInfoResult& _return, const general::Address& token) {
    bool found = false;

    const csdb::Address addr = BlockChain::getAddressFromKey(token);
    tm.loadTokenInfo(std::vector<csdb::Address>(1, addr), [&token, &addr, &found, &_return](const TokensMap& tm, const HoldersMap&) {
        auto tIt = tm.find(addr);
        if (tIt != tm.end()) {
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
    tm.loadTokenInfo(std::vector<csdb::Address>(1, addr), [&token, &addr, &found, &offset, &limit, &_return, comparator](const TokensMap& tm, const HoldersMap&) {
        auto tIt = tm.find(addr);
        if (tIt != tm.end()) {
            found = true;
            _return.count = (uint32_t) tIt->second.realHoldersCount;

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
                th.transfersCount = (uint32_t) t.second.transfersCount;

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
        case TL_Address:
            comparator = [desc](const VT& lhs, const VT& rhs) { return desc ^ (lhs.first < rhs.first); };
            break;
        case TL_TotalSupply:
            comparator = [desc](const VT& lhs, const VT& rhs) { return desc ^ (stod(lhs.second.totalSupply) < stod(rhs.second.totalSupply)); };
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
    };

    std::vector<csdb::Address> sortTokenAddrs;
    tm.loadTokenInfo({}, [&, comparator](const TokensMap& tm, const HoldersMap&) {
        _return.count = (uint32_t)tm.size();
        applyToSortedMap(tm, comparator, [&](const TokensMap::value_type& t) {
            if (--offset >= 0)
                return true;

            api::TokenInfo tok;
            putTokenInfo(tok, fromByteArray(t.first.public_key()), t.second);
            sortTokenAddrs.push_back(BlockChain::getAddressFromKey(tok.address));

            if (--limit == 0)
                return false;

            return true;
        });
    });

    tm.loadTokenInfo(sortTokenAddrs, [&](const TokensMap& tm, const HoldersMap&) {
        api::TokenInfo tok;
        for (auto t : tm) {
            if ((t.second.name.find(filters.name) != std::string::npos && t.second.symbol.find(filters.code) != std::string::npos && t.second.tokenStandard == filters.tokenStandard) ||
                (t.second.name.find(filters.name) && t.second.symbol.find(filters.code) && !t.second.tokenStandard) ||
                (t.second.name.find(filters.name) && filters.code.empty() && !t.second.tokenStandard) ||
                (filters.name.empty() && t.second.symbol.find(filters.code) && t.second.tokenStandard) ||
                (filters.name.empty() && t.second.symbol.find(filters.code) && !t.second.tokenStandard) ||
                (filters.name.empty() && filters.code.empty() && t.second.tokenStandard) ||
                (filters.name.empty() && filters.code.empty() && !t.second.tokenStandard)) {
                    putTokenInfo(tok, fromByteArray(t.first.public_key()), t.second);
                    _return.tokens.push_back(tok);
            }
        }
    });   

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

//////////Wallets
typedef std::list<std::pair<const cs::WalletsCache::WalletData::Address*, const cs::WalletsCache::WalletData*>> WCSortedList;
template <typename T>
void walletStep(const cs::WalletsCache::WalletData::Address* addr, const cs::WalletsCache::WalletData* wd, const uint64_t num,
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

    bc.iterateOverWallets([&lst, num, getter, comparator](const cs::WalletsCache::WalletData::Address& addr, const cs::WalletsCache::WalletData& wd) {
        if (!addr.empty() && wd.balance_ >= csdb::Amount(0)) {
            walletStep(&addr, &wd, num, getter, comparator, lst);
        }
        return true;
    });
}

void APIHandler::WalletsGet(WalletsGetResult& _return, int64_t _offset, int64_t _limit, int8_t _ordCol, bool _desc) {
    if (!validatePagination(_return, *this, _offset, _limit)) {
        return;
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);

    WCSortedList lst;
    const uint64_t num = _offset + _limit;

    if (_ordCol == 0) {  // Balance
        iterateOverWallets<csdb::Amount>([](const cs::WalletsCache::WalletData& wd) -> const csdb::Amount& { return wd.balance_; }, num, _desc, lst, s_blockchain);
    }
#ifdef MONITOR_NODE
    else if (_ordCol == 1) {  // TimeReg
        iterateOverWallets<uint64_t>([](const cs::WalletsCache::WalletData& wd) -> const uint64_t& { return wd.createTime_; }, num, _desc, lst, s_blockchain);
    }
    else {  // Tx count
        iterateOverWallets<uint64_t>([](const cs::WalletsCache::WalletData& wd) -> const uint64_t& { return wd.transNum_; }, num, _desc, lst, s_blockchain);
    }
#endif

    if (lst.size() < (uint64_t)_offset) {
        return;
    }

    auto ptr = lst.begin();
    std::advance(ptr, _offset);

    for (; ptr != lst.end(); ++ptr) {
        api::WalletInfo wi;
        const cs::Bytes addr_b((*(ptr->first)).begin(), (*(ptr->first)).end());
        wi.address = fromByteArray(addr_b);
        wi.balance.integral = ptr->second->balance_.integral();
        wi.balance.fraction = ptr->second->balance_.fraction();
#ifdef MONITOR_NODE
        wi.transactionsNumber = ptr->second->transNum_;
        wi.firstTransactionTime = ptr->second->createTime_;
#endif

        _return.wallets.push_back(wi);
    }

    _return.count = (uint32_t) s_blockchain.getWalletsCountWithBalance();
}

void APIHandler::TrustedGet(TrustedGetResult& _return, int32_t _page) {
#ifdef MONITOR_NODE
    const static uint32_t PER_PAGE = 256;
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
    _page = std::max(int32_t(0), _page);

    uint32_t offset = _page * PER_PAGE;
    uint32_t limit = PER_PAGE;
    uint32_t total = 0;

    s_blockchain.iterateOverWriters([&_return, &offset, &limit, &total](const cs::WalletsCache::WalletData::Address& addr, const cs::WalletsCache::TrustedData& wd) {
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

////////new

void APIHandler::SyncStateGet(api::SyncStateResult& _return) {
    _return.lastBlock = s_blockchain.getLastSeq();
    _return.currRound = cs::Conveyer::instance().currentRoundNumber();
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void apiexec::APIEXECHandler::GetSeed(apiexec::GetSeedResult& _return, const general::AccessID accessId) {
    if (accessId == executor::Executor::ACCESS_ID_RESERVE::GETTER) { // for getter
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
    // const csdb::Address addr = BlockChain::getAddressFromKey(transaction.source);
    // BlockChain::WalletData wallData{};
    // BlockChain::WalletId wallId{};
    // if (!blockchain_.findWalletData(addr, wallData, wallId)) {
    //  SetResponseStatus(_return.status, APIRequestStatusType::NOT_FOUND);
    //  return;
    //}
    // const_cast<api::Transaction&>(transaction).id = wallData.trxTail_.empty() ? 0 : wallData.trxTail_.getLastTransactionId() + 1;
    executor_.addInnerSendTransaction(accessId, executor_.make_transaction(transaction));
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
    _return.walletId = wallId;
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
    const auto sci = deserialize<api::SmartContractInvocation>(trxn.user_field(0).value<std::string>());
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
    BlockChain::WalletId wallId{};
    if (!blockchain_.findWalletData(addr, wallData, wallId)) {
        _return.balance.integral = 0;
        _return.balance.fraction = 0;
    }
    else {
        _return.balance.integral = wallData.balance_.integral();
        _return.balance.fraction = static_cast<decltype(_return.balance.fraction)>(wallData.balance_.fraction());
    }
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void apiexec::APIEXECHandler::PoolGet(PoolGetResult& _return, const int64_t sequence) {
    auto poolBin = executor_.loadBlockApi(static_cast<cs::Sequence>(sequence)).to_binary();
    _return.pool.reserve(poolBin.size());
    std::copy(poolBin.begin(), poolBin.end(), std::back_inserter(_return.pool));
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

// Executor implementation

namespace executor {

    void Executor::executeByteCode(executor::ExecuteByteCodeResult& resp, const std::string& address, const std::string& smart_address, const std::vector<general::ByteCodeObject>& code,
        const std::string& state, std::vector<MethodHeader>& methodHeader, bool isGetter, cs::Sequence sequence) {
        static std::mutex mutex;
        std::lock_guard lock(mutex);  // temporary solution

        if (!code.empty()) {
            executor::SmartContractBinary smartContractBinary;
            smartContractBinary.contractAddress = smart_address;
            smartContractBinary.object.byteCodeObjects = code;
            smartContractBinary.object.instance = state;
            smartContractBinary.stateCanModify = solver_.isContractLocked(BlockChain::getAddressFromKey(smart_address)) ? true : false;
            if (auto optOriginRes = execute(address, smartContractBinary, methodHeader, isGetter, sequence)) {
                resp = optOriginRes.value().resp;
            }
        }
    }

    void Executor::executeByteCodeMultiple(ExecuteByteCodeMultipleResult& _return, const ::general::Address& initiatorAddress, const SmartContractBinary& invokedContract,
        const std::string& method, const std::vector<std::vector<::general::Variant>>& params, const int64_t executionTime, cs::Sequence sequence) {
        if (!connect()) {
            _return.status.code = 1;
            _return.status.message = "No executor connection!";
            return;
        }
        const auto access_id = generateAccessId(sequence);
        ++execCount_;
        try {
            std::shared_lock lock(sharedErrorMutex_);
            origExecutor_->executeByteCodeMultiple(_return, access_id, initiatorAddress, invokedContract, method, params, executionTime, EXECUTOR_VERSION);
        }
        catch (::apache::thrift::transport::TTransportException& x) {
            // sets stop_ flag to true forever, replace with new instance
            if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
                reCreationOriginExecutor();
            }
            _return.status.code = 1;
            _return.status.message = x.what();
        }
        catch (std::exception& x) {
            _return.status.code = 1;
            _return.status.message = x.what();
        }
        --execCount_;
        deleteAccessId(access_id);
        disconnect();
    }

    std::optional<std::string> Executor::getState(const csdb::Address& p_address) {
        csdb::Address abs_addr = blockchain_.getAddressByType(p_address, BlockChain::AddressType::PublicKey);
        if (!abs_addr.is_valid()) {
            return std::nullopt;
        }
        std::string state = cs::SmartContracts::get_contract_state(blockchain_, abs_addr);
        if (state.empty()) {
            return std::nullopt;
        }
        return std::make_optional(std::move(state));
    }

    void Executor::state_update(const csdb::Pool& pool) {
        if (!pool.transactions().size())
            return;
        for (const auto& trxn : pool.transactions()) {
            if (trxn.is_valid() && cs::SmartContracts::is_state_updated(trxn)) {
                const auto address = blockchain_.getAddressByType(trxn.target(), BlockChain::AddressType::PublicKey);
                const auto newstate = cs::SmartContracts::get_contract_state(blockchain_, address);
                if (!newstate.empty()) {
                    setLastState(address, newstate);
                    updateCacheLastStates(address, pool.sequence(), newstate);
                }
            }
        }
    }

    std::optional<Executor::ExecuteResult> Executor::executeTransaction(const std::vector<ExecuteTransactionInfo>& smarts, std::string forceContractState) {
        std::lock_guard lock(callExecutorLock_);  // temporary solution

        if (smarts.empty()) {
            return std::nullopt;
        }
        const auto& head_transaction = smarts[0].transaction;
        const auto& deployTrxn = smarts[0].deploy;
        if (!head_transaction.is_valid() || !deployTrxn.is_valid()) {
            return std::nullopt;
        }
        // all smarts must have the same initiator and address
        const auto source = head_transaction.source();
        const auto target = head_transaction.target();
        for (const auto& smart : smarts) {
            if (source != smart.transaction.source() || target != smart.transaction.target()) {
                return std::nullopt;
            }
        }

        auto smartSource = blockchain_.getAddressByType(source, BlockChain::AddressType::PublicKey);
        auto smartTarget = blockchain_.getAddressByType(target, BlockChain::AddressType::PublicKey);

        // get deploy transaction
        const auto isdeploy = (head_transaction.id() == deployTrxn.id()); //isDeploy(head_transaction);
        //if (!isdeploy) {  // execute
        //    const auto optDeployId = getDeployTrxn(smartTarget);
        //    if (!optDeployId.has_value()) {
        //        return std::nullopt;
        //    }
        //    deployTrxn = loadTransactionApi(optDeployId.value());
        //}
        //else {
        //    deployTrxn = head_transaction;
        //}

        // fill smartContractBinary
        const auto sci_deploy = deserialize<api::SmartContractInvocation>(deployTrxn.user_field(cs::trx_uf::deploy::Code).value<std::string>());
        executor::SmartContractBinary smartContractBinary;
        smartContractBinary.contractAddress = smartTarget.to_api_addr();
        smartContractBinary.object.byteCodeObjects = sci_deploy.smartContractDeploy.byteCodeObjects;
        // may contain temporary last new state not yet written into block chain (to allow "speculative" multi-executions of the same contract)
        if (!isdeploy) {
            if (!forceContractState.empty()) {
                smartContractBinary.object.instance = forceContractState;
            }
            else {
                auto optState = getState(smartTarget);
                if (optState.has_value()) {
                    smartContractBinary.object.instance = optState.value();
                }
            }
        }
        smartContractBinary.stateCanModify = solver_.isContractLocked(BlockChain::getAddressFromKey(smartTarget.to_api_addr()));

        // fill methodHeader
        std::vector<executor::MethodHeader> methodHeader;
        for (const auto& smart_item : smarts) {
            executor::MethodHeader header;
            const csdb::Transaction& smart = smart_item.transaction;
            if (smart_item.convention != MethodNameConvention::Default) {
                // call to payable
                // add method name
                header.methodName = "payable";
                // add arg[0]
                general::Variant& var0 = header.params.emplace_back(::general::Variant{});
                std::string str_val = smart.amount().to_string();
                if (smart_item.convention == MethodNameConvention::PayableLegacy) {
                    var0.__set_v_string(str_val);
                }
                else {
                    var0.__set_v_big_decimal(str_val);
                }
                // add arg[1]
                str_val.clear();
                if (smart.user_field(1).is_valid()) {
                    str_val = smart.user_field(1).value<std::string>();
                }
                general::Variant& var1 = header.params.emplace_back(::general::Variant{});
                if (smart_item.convention == MethodNameConvention::PayableLegacy) {
                    var1.__set_v_string(str_val);
                }
                else {
                    var1.__set_v_byte_array(str_val);
                }
            }
            else {
                api::SmartContractInvocation sci;
                const auto fld = smart.user_field(0);
                if (!fld.is_valid()) {
                    return std::nullopt;
                }
                else if (!isdeploy) {
                    sci = deserialize<api::SmartContractInvocation>(fld.value<std::string>());
                    header.methodName = sci.method;
                    header.params = sci.params;

                    for (const auto& addrLock : sci.usedContracts) {
                        addToLockSmart(addrLock, getFutureAccessId());
                    }
                }
            }
            methodHeader.push_back(header);
        }

        const auto optOriginRes = execute(smartSource.to_api_addr(), smartContractBinary, methodHeader, false /*isGetter*/, smarts[0].sequence /*sequence*/);

        for (const auto& smart : smarts) {
            if (!isdeploy) {
                if (smart.convention == MethodNameConvention::Default) {
                    const auto fld = smart.transaction.user_field(0);
                    if (fld.is_valid()) {
                        auto sci = deserialize<api::SmartContractInvocation>(smart.transaction.user_field(0).value<std::string>());
                        for (const auto& addrLock : sci.usedContracts) {
                            deleteFromLockSmart(addrLock, getFutureAccessId());
                        }
                    }
                }
            }
        }

        if (!optOriginRes.has_value()) {
            return {};
        }

        //const auto optInnerTransactions = getInnerSendTransactions(optOriginRes.value().acceessId);

        // fill res
        ExecuteResult res;
        res.response = optOriginRes.value().resp.status;

        //if (optInnerTransactions.has_value())
        //    res.trxns = optInnerTransactions.value();

        deleteInnerSendTransactions(optOriginRes.value().acceessId);
        res.selfMeasuredCost = (long)optOriginRes.value().timeExecute;

        for (const auto& setters : optOriginRes.value().resp.results) {
            auto& smartRes = res.smartsRes.emplace_back(ExecuteResult::SmartRes{});
            smartRes.retValue       = setters.ret_val;
            smartRes.executionCost  = setters.executionCost;
            smartRes.response       = setters.status;
           
            for (auto& states : setters.contractsState) {          // state
                auto addr = BlockChain::getAddressFromKey(states.first);
                smartRes.states[BlockChain::getAddressFromKey(states.first)] = states.second;
            }
                   
            for (auto transaction : setters.emittedTransactions) { // emittedTransactions    
                ExecuteResult::EmittedTrxn emittedTrxn;
                emittedTrxn.source   = BlockChain::getAddressFromKey(transaction.source);
                emittedTrxn.target   = BlockChain::getAddressFromKey(transaction.target);
                emittedTrxn.amount   = csdb::Amount(transaction.amount.integral, transaction.amount.fraction);
                emittedTrxn.userData = transaction.userData;
                smartRes.emittedTransactions.push_back(emittedTrxn);
            }
        }

        return res;
    }

    std::optional<Executor::ExecuteResult> Executor::reexecuteContract(ExecuteTransactionInfo& contract, std::string forceContractState) {
        std::lock_guard lock(callExecutorLock_);  // temporary solution

        if (!contract.transaction.is_valid() || !contract.deploy.is_valid()) {
            return std::nullopt;
        }
        auto smartSource = blockchain_.getAddressByType(contract.transaction.source(), BlockChain::AddressType::PublicKey);
        auto smartTarget = blockchain_.getAddressByType(contract.transaction.target(), BlockChain::AddressType::PublicKey);

        // get deploy transaction
        const csdb::Transaction& deployTrxn = contract.deploy;
        const auto isdeploy = (contract.deploy.id() == contract.transaction.id()); // isDeploy(contract.transaction);
        //if (!isdeploy) {  // execute
        //    const auto optDeployId = getDeployTrxn(smartTarget);
        //    if (!optDeployId.has_value()) {
        //        return std::nullopt;
        //    }
        //    deployTrxn = loadTransactionApi(optDeployId.value());
        //}
        //else {
        //    deployTrxn = contract.transaction;
        //}

        // fill smartContractBinary
        const auto sci_deploy = deserialize<api::SmartContractInvocation>(deployTrxn.user_field(0).value<std::string>());
        executor::SmartContractBinary smartContractBinary;
        smartContractBinary.contractAddress = smartTarget.to_api_addr();
        smartContractBinary.object.byteCodeObjects = sci_deploy.smartContractDeploy.byteCodeObjects;
        // may contain temporary last new state not yet written into block chain (to allow "speculative" multi-executions af the same contract)
        if (!isdeploy) {
            if (!forceContractState.empty()) {
                smartContractBinary.object.instance = forceContractState;
            }
            else {
                auto optState = getState(smartTarget);
                if (optState.has_value()) {
                    smartContractBinary.object.instance = optState.value();
                }
            }
        }
        smartContractBinary.stateCanModify = true;

        // fill methodHeader
        std::vector<executor::MethodHeader> methodHeader;

        executor::MethodHeader header;
        if (contract.convention != MethodNameConvention::Default) {
            // call to payable
            // add method name
            header.methodName = "payable";
            // add arg[0]
            general::Variant& var0 = header.params.emplace_back(::general::Variant{});
            std::string str_val = contract.transaction.amount().to_string();
            if (contract.convention == MethodNameConvention::PayableLegacy) {
                var0.__set_v_string(str_val);
            }
            else {
                var0.__set_v_big_decimal(str_val);
            }
            // add arg[1]
            str_val.clear();
            if (contract.transaction.user_field(1).is_valid()) {
                str_val = contract.transaction.user_field(1).value<std::string>();
            }
            general::Variant& var1 = header.params.emplace_back(::general::Variant{});
            if (contract.convention == MethodNameConvention::PayableLegacy) {
                var1.__set_v_string(str_val);
            }
            else {
                var1.__set_v_byte_array(str_val);
            }
        }
        else {
            api::SmartContractInvocation sci;
            const auto fld = contract.transaction.user_field(0);
            if (!fld.is_valid()) {
                return std::nullopt;
            }
            else if (!isdeploy) {
                sci = deserialize<api::SmartContractInvocation>(fld.value<std::string>());
                header.methodName = sci.method;
                header.params = sci.params;

                for (const auto& addrLock : sci.usedContracts) {
                    addToLockSmart(addrLock, getFutureAccessId());
                }
            }
        }
        methodHeader.push_back(header);

        const auto optOriginRes = execute(smartSource.to_api_addr(), smartContractBinary, methodHeader, false /*! isGetter*/, contract.sequence);

        if (!isdeploy) {
            if (contract.convention == MethodNameConvention::Default) {
                const auto fld = contract.transaction.user_field(0);
                if (fld.is_valid()) {
                    auto sci = deserialize<api::SmartContractInvocation>(contract.transaction.user_field(0).value<std::string>());
                    for (const auto& addrLock : sci.usedContracts) {
                        deleteFromLockSmart(addrLock, getFutureAccessId());
                    }
                }
            }
        }

        if (!optOriginRes.has_value()) {
            return {};
        }

        //const auto optInnerTransactions = getInnerSendTransactions(optOriginRes.value().acceessId);

        // fill res
        ExecuteResult res;
        res.response = optOriginRes.value().resp.status;

        //if (optInnerTransactions.has_value())
        //    res.trxns = optInnerTransactions.value();

        deleteInnerSendTransactions(optOriginRes.value().acceessId);
        res.selfMeasuredCost = (long)optOriginRes.value().timeExecute;

        for (const auto& setters : optOriginRes.value().resp.results) {
            auto& smartRes = res.smartsRes.emplace_back(ExecuteResult::SmartRes{});
            smartRes.retValue = setters.ret_val;
            smartRes.executionCost = setters.executionCost;
            smartRes.response = setters.status;

            for (auto& states : setters.contractsState) {          // state
                auto addr = BlockChain::getAddressFromKey(states.first);
                smartRes.states[BlockChain::getAddressFromKey(states.first)] = states.second;
            }

            for (auto transaction : setters.emittedTransactions) { // emittedTransactions    
                ExecuteResult::EmittedTrxn emittedTrxn;
                emittedTrxn.source = BlockChain::getAddressFromKey(transaction.source);
                emittedTrxn.target = BlockChain::getAddressFromKey(transaction.target);
                emittedTrxn.amount = csdb::Amount(transaction.amount.integral, transaction.amount.fraction);
                emittedTrxn.userData = transaction.userData;
                smartRes.emittedTransactions.push_back(emittedTrxn);
            }
        }

        return res;
    }

    // explicit_sequence set the proper context while executing transaction
    std::optional<Executor::OriginExecuteResult> Executor::execute(const std::string& address,
        const SmartContractBinary& smartContractBinary, std::vector<MethodHeader>& methodHeader, bool isGetter, cs::Sequence explicit_sequence) {
        constexpr uint64_t EXECUTION_TIME = Consensus::T_smart_contract;
        OriginExecuteResult originExecuteRes{};
        if (!connect()) {
            return std::nullopt;
        }

        uint64_t access_id{};
        if (!isGetter)
            access_id = generateAccessId(explicit_sequence);

        //const auto access_id = generateAccessId();
        ++execCount_;
        const auto timeBeg = std::chrono::steady_clock::now();
        try {
            std::shared_lock lock(sharedErrorMutex_);
            origExecutor_->executeByteCode(originExecuteRes.resp, access_id, address, smartContractBinary, methodHeader, EXECUTION_TIME, EXECUTOR_VERSION);
        }
        catch (::apache::thrift::transport::TTransportException& x) {
            // sets stop_ flag to true forever, replace with new instance
            if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
                reCreationOriginExecutor();
            }
            originExecuteRes.resp.status.code = cs::error::ThriftException;
            originExecuteRes.resp.status.message = x.what();
        }
        catch (std::exception& x) {
            originExecuteRes.resp.status.code = cs::error::StdException;
            originExecuteRes.resp.status.message = x.what();
        }
        originExecuteRes.timeExecute = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - timeBeg).count();
        --execCount_;
        if (!isGetter)
            deleteAccessId(access_id);
        disconnect();
        originExecuteRes.acceessId = access_id;
        return std::make_optional<OriginExecuteResult>(std::move(originExecuteRes));
    }

} // Executor namespace

/**
// End of C:\work\node\api\src\apihandler.cpp
 */
