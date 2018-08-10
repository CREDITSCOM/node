#define TRACE_ENABLER

#include <APIHandler.h>
#include <DebugLog.h>

#include "csconnector/csconnector.h"

// csdb
#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/csdb.h>
#include <csdb/currency.h>
#include <csdb/pool.h>
#include <csdb/storage.h>
#include <csdb/transaction.h>
#include <csdb/wallet.h>

#include <algorithm>
#include <cassert>
#include <type_traits>
#include <experimental/type_traits>

#include <api_types.h>

#include <lib/system/logger.hpp>

#include <iomanip>

#include <boost/io/ios_state.hpp>

#include <scope_guard.h>

using namespace api;
#define TRACE()

APIHandler::APIHandler(BlockChain& blockchain,
                       Credits::ISolver& _solver)
  : s_blockchain(blockchain)
  , solver(_solver)
  , stats(blockchain)
  , executor_transport(new thrift::transport::TBufferedTransport(
      thrift::stdcxx::make_shared<thrift::transport::TSocket>("localhost",
                                                              9080)))
  , executor(thrift::stdcxx::make_shared<thrift::protocol::TBinaryProtocol>(
      executor_transport))
  , last_seen_contract_block()
{
    TRACE();
    std::cerr << (s_blockchain.isGood() ? "Storage is opened normal"
                                        : "Storage is not opened")
              << std::endl;
    TRACE();
    if (!s_blockchain.isGood()) {
        return;
    }
    TRACE();
    update_smart_caches();
    TRACE();
}

void
APIHandlerBase::SetResponseStatus(APIResponse& response,
                                  APIRequestStatusType status,
                                  const std::string& details)
{
    struct APIRequestStatus
    {
        APIRequestStatus(uint8_t code, std::string message)
          : message(message)
          , code(code)
        {}
        std::string message;
        uint8_t code;
    };

    APIRequestStatus statuses[static_cast<size_t>(
      APIHandlerBase::APIRequestStatusType::MAX)] = {
        {
          0,
          "Success",
        },
        {
          1,
          "Failure",
        },
        { 2, "Not Implemented" },
    };
    response.code = statuses[static_cast<uint8_t>(status)].code;
    response.message = statuses[static_cast<uint8_t>(status)].message + details;
}

void
APIHandlerBase::SetResponseStatus(APIResponse& response, bool commandWasHandled)
{
    SetResponseStatus(response,
                      (commandWasHandled
                         ? APIRequestStatusType::SUCCESS
                         : APIRequestStatusType::NOT_IMPLEMENTED));
}

void
APIHandler::BalanceGet(BalanceGetResult& _return,
                       const Address& address,
                       const Currency& currency)
{
    csdb::Address addr;
    // if (address.size() != 64)
    addr = BlockChain::getAddressFromKey(address);
    // else
    //    addr = csdb::Address::from_string(address);

    csdb::Amount result = s_blockchain.getBalance(addr);

    _return.amount.integral = result.integral();
    _return.amount.fraction = result.fraction();

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

std::string
fromByteArray(const ::csdb::internal::byte_array& bar)
{
    std::string res;
    {
        res.reserve(bar.size());
        std::transform(bar.begin(),
                       bar.end(),
                       std::back_inserter<std::string>(res),
                       [](uint8_t _) { return char(_); });
    }
    return res;
}

::csdb::internal::byte_array
toByteArray(const std::string& s)
{
    ::csdb::internal::byte_array res;
    {
        res.reserve(s.size());
        std::transform(s.begin(),
                       s.end(),
                       std::back_inserter<decltype(res)>(res),
                       [](uint8_t _) { return uint8_t(_); });
    }
    return res;
}

template<typename T>
T
deserialize(std::string s)
{
    // https://stackoverflow.com/a/16261758/2016154
    static_assert(
                  CHAR_BIT == 8 && std::experimental::is_same_v<std::uint8_t, unsigned char>,
      "This code requires std::uint8_t to be implemented as unsigned char.");

    auto buffer = stdcxx::make_shared<TMemoryBuffer>(
      reinterpret_cast<uint8_t*>(&(s[0])), (uint32_t)s.size());
    TBinaryProtocol proto(buffer);
    T sc;
    sc.read(&proto);
    return sc;
}

template<typename T>
std::string
serialize(const T& sc)
{
    auto buffer = stdcxx::make_shared<TMemoryBuffer>();
    TBinaryProtocol proto(buffer);
    sc.write(&proto);
    return buffer->getBufferAsString();
}

api::Amount
convertAmount(const csdb::Amount& amount)
{
    api::Amount result;
    result.integral = amount.integral();
    result.fraction = amount.fraction();
    assert(result.fraction >= 0);
    return result;
}

api::TransactionId
convert_transaction_id(const csdb::TransactionID& trid)
{
    api::TransactionId result_id;
    result_id.index = trid.index();
    result_id.poolHash = fromByteArray(trid.pool_hash().to_binary());
    return result_id;
}

csdb::TransactionID
convert_transaction_id(const api::TransactionId& trid)
{
    return csdb::TransactionID(
      csdb::PoolHash::from_binary(toByteArray(trid.poolHash)), trid.index);
}

api::SealedTransaction
convertTransaction(const csdb::Transaction& transaction)
{
    api::SealedTransaction result;
    const csdb::Amount& amount = transaction.amount();
    const csdb::Currency& currency = transaction.currency();
    const csdb::Address& target = transaction.target();
    const csdb::TransactionID& id = transaction.id();
    const csdb::Address& address = transaction.source();

    result.id.index = id.index();
    result.id.poolHash = fromByteArray(id.pool_hash().to_binary());

    result.trxn.amount = convertAmount(amount);
    result.trxn.currency = currency.to_string();

    result.trxn.source = fromByteArray(address.public_key());
    result.trxn.target = fromByteArray(target.public_key());

    auto uf = transaction.user_field(0);
    if (result.trxn.__isset.smartContract = uf.is_valid()) { // non-bug
        result.trxn.smartContract =
          deserialize<api::SmartContractInvocation>(uf.value<std::string>());
    }

    return result;
}

std::vector<api::SealedTransaction>
convertTransactions(const std::vector<csdb::Transaction>& transactions)
{
    std::vector<api::SealedTransaction> result;
    // reserve vs resize
    result.resize(transactions.size());
    std::transform(transactions.begin(),
                   transactions.end(),
                   result.begin(),
                   convertTransaction);
    return result;
}

api::Pool
APIHandler::convertPool(const csdb::Pool& pool)
{
    api::Pool result;
    pool.is_valid();
    if (pool.is_valid()) {
        result.hash = fromByteArray(pool.hash().to_binary());
        result.poolNumber = pool.sequence();
        assert(result.poolNumber >= 0);
        result.prevHash = fromByteArray(pool.previous_hash().to_binary());
        // std::cerr << pool.user_field(0).value<std::string>() << std::endl;
        result.time = atoll(
          pool.user_field(0)
            .value<std::string>()
            .c_str()); // atoll(pool.user_field(0).value<std::string>().c_str());

        result.transactionsCount =
          (int32_t)pool.transactions_count(); // DO NOT EVER CREATE POOLS WITH
                                              // MORE THAN 2 BILLION
                                              // TRANSACTIONS, EVEN AT NIGHT
    }
    return result;
}

api::Pool
APIHandler::convertPool(const csdb::PoolHash& poolHash)
{
    const csdb::Pool& pool = s_blockchain.loadBlock(poolHash);
    return convertPool(pool);
}

std::vector<api::SealedTransaction>
extractTransactions(const csdb::Pool& pool, int64_t limit, const int64_t offset)
{
    int64_t transactionsCount = pool.transactions_count();
    assert(transactionsCount >= 0);

    std::vector<api::SealedTransaction> result;

    if (offset > transactionsCount) {
        return result; // если запрашиваемые
                       // транзакций выходят за
        // пределы пула возвращаем пустой результат
    }
    transactionsCount -=
      offset; // мы можем отдать все транзакции в пуле за вычетом смещения

    if (limit > transactionsCount)
        limit = transactionsCount; // лимит уменьшается до реального количества
                                   // транзакций которые можно отдать

    for (int64_t index = offset; index < (offset + limit); ++index) {
        const csdb::Transaction transaction = pool.transaction(index);
        result.push_back(convertTransaction(transaction));
    }
    return result;
}

void
APIHandler::TransactionGet(TransactionGetResult& _return,
                           const TransactionId& transactionId)
{
    Log("TransactionGet");

    const csdb::PoolHash& poolhash =
      csdb::PoolHash::from_binary(toByteArray(transactionId.poolHash));
    const csdb::TransactionID& tmpTransactionId =
      csdb::TransactionID(poolhash, (transactionId.index));
    const csdb::Transaction& transaction =
      s_blockchain.loadTransaction(tmpTransactionId);

    _return.found = transaction.is_valid();
    if (_return.found) {
        _return.transaction = convertTransaction(transaction);
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
APIHandler::TransactionsGet(TransactionsGetResult& _return,
                            const Address& address,
                            int64_t offset,
                            const int64_t limit)
{
    Log("TransactionsGet");

    csdb::Address addr;
    // if (address.size() != 64)
    addr = BlockChain::getAddressFromKey(address);
    // else
    //    addr = csdb::Address::from_string(address);

    BlockChain::Transactions transactions;

    s_blockchain.getTransactions(transactions, addr, offset, limit);

    _return.transactions = convertTransactions(transactions);

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

api::SmartContractInvocation
fetch_smart(const csdb::Transaction& tr)
{
    return tr.is_valid() ? deserialize<api::SmartContractInvocation>(
                             tr.user_field(0).value<std::string>())
                         : api::SmartContractInvocation();
}

api::SmartContract
fetch_smart_body(const csdb::Transaction& tr)
{
    api::SmartContract res;
    if (!tr.is_valid()) {
        return res;
    }
    auto sci = deserialize<api::SmartContractInvocation>(
      tr.user_field(0).value<std::string>());
    res.byteCode = sci.byteCode;
    res.sourceCode = sci.sourceCode;
    res.hashState = sci.hashState;

    res.deployer = fromByteArray(tr.source().public_key());
    res.address = fromByteArray(tr.target().public_key());

    return res;
}

bool
is_smart(const csdb::Transaction& tr)
{
    csdb::UserField uf = tr.user_field(0);
    return uf.type() == csdb::UserField::Type::String;
}

bool
is_smart_deploy(const api::SmartContractInvocation& smart)
{
    return !smart.byteCode.empty();
}

//
// class ToHex
//{
//    const std::string& s;
//
//  public:
//    ToHex(const std::string& s)
//      : s(s)
//    {}
//
//    friend std::ostream& operator<<(std::ostream& os, const ToHex& th);
//};

//////////std::ostream&
//////////operator<<(std::ostream& os, const ToHex& th)
//////////{
//////////    boost::io::ios_flags_saver ifs(os);
//////////    for (auto c : th.s) {
//////////        os << std::hex << std::setfill('0') << std::setw(2) <<
/// std::nouppercase
//////////           << (int)c;
//////////    }
//////////
//////////    return os;
//////////}

void
APIHandler::TransactionFlow(api::TransactionFlowResult& _return,
                            const Transaction& transaction)
{
    if (transaction.target == "accXpfvxnZa8txuxpjyPqzBaqYPHqYu2rwn34lL8rjI=") {
        return;
    }

    TRACE();

    csdb::Transaction send_transaction;
    PublicKey from, to;

    auto source = BlockChain::getAddressFromKey(transaction.source);

    const uint64_t WALLET_DENOM = 1'000'000'000'000'000'000ull;

    send_transaction.set_amount(csdb::Amount(
      transaction.amount.integral, transaction.amount.fraction, WALLET_DENOM));
    send_transaction.set_balance(s_blockchain.getBalance(source));
    send_transaction.set_currency(csdb::Currency("CS"));
    send_transaction.set_source(source);
    send_transaction.set_target(
      BlockChain::getAddressFromKey(transaction.target));

    TRACE();

    if (!transaction.__isset.smartContract) {
        solver.send_wallet_transaction(send_transaction);
        SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
        return;
    }

    const auto smart_addr = send_transaction.target();

    TRACE();

    update_smart_caches();
    TRACE();

    auto input_smart = transaction.smartContract;

    bool deploy = input_smart.method.empty();
    if (!deploy) {
        input_smart.byteCode = std::string();
        input_smart.sourceCode = std::string();
    }

    bool present = false;
    std::string origin_bytecode;
    {
        TRACE();
        decltype(auto) smart_origin = locked_ref(this->smart_origin);
        TRACE();
        auto it = smart_origin->find(smart_addr);
        if (present = it != smart_origin->end()) {
            origin_bytecode =
              fetch_smart(s_blockchain.loadTransaction(it->second)).byteCode;
        }
    }

    if (present == deploy) {
        TRACE();
        SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
        return;
    }

    const std::string& bytecode =
      deploy ? input_smart.byteCode : origin_bytecode;

    bool amnesia = input_smart.forgetNewState;

    auto& fresh_contract_state = [this, &smart_addr]() -> decltype(auto) {
        TRACE();
        auto smart_state(locked_ref(this->smart_state));
        TRACE();
        return (*smart_state)[smart_addr];
    }();
    std::string contract_state;

    while (
      !deploy) { // actually never changes, was too ugly to write an if clause
        TRACE();
        {
            TRACE();
            auto p_fresh_contract_state = locked_ref(fresh_contract_state);
            TRACE();
            if (!p_fresh_contract_state->empty()) {
                TRACE();
                if (!amnesia) {
                    TRACE();
                    contract_state = std::move(*p_fresh_contract_state);
                    p_fresh_contract_state->clear();
                    assert(p_fresh_contract_state->empty());
                }
                break;
            }
            TRACE();
        }
        TRACE();
        s_blockchain.wait_for_block();
        TRACE();
        update_smart_caches();
        TRACE();
    }
    auto sg =
      scopeGuard([this, &contract_state, &fresh_contract_state, amnesia]() {
          TRACE();
          if (amnesia) {
              return;
          }
          TRACE();
          auto p_fresh_contract_state = locked_ref(fresh_contract_state);
          TRACE();
          *p_fresh_contract_state = std::move(contract_state);
      });

    executor::APIResponse api_resp;

    TRACE();

    static auto transport_opened = false;
    if (!transport_opened) {
        transport_opened = true;
        executor_transport->open();
    }

    TRACE();

    {
        executor.executeByteCode(api_resp,
                                 transaction.source,
                                 bytecode,
                                 amnesia ? ([&]() -> decltype(auto) {
                                     TRACE();
                                     auto p_fresh_contract_state =
                                       locked_ref(fresh_contract_state);
                                     TRACE();
                                     return *p_fresh_contract_state;
                                 }())
                                         : contract_state,
                                 input_smart.method,
                                 input_smart.params);
    }

    if (api_resp.code) {
        TRACE();
        _return.status.code = api_resp.code;
        _return.status.message = api_resp.message;
        return;
    }

    if (_return.__isset.smart_contract_result =
          api_resp.__isset.ret_val) { // non-bug = instead of ==
        TRACE();
        _return.smart_contract_result = api_resp.ret_val;
    }

    TRACE();

    if (amnesia) {
        TRACE();
        SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
        return;
    }
    TRACE();

    sg.dismiss();
    TRACE();

    send_transaction.set_amount(csdb::Amount(1));
    send_transaction.add_user_field(0, serialize(input_smart));
    send_transaction.add_user_field(1, api_resp.contractState);
    solver.send_wallet_transaction(send_transaction);

    TRACE();

    if (deploy) { // in case "if (true) { ... }" would not execute check
                  // also "deploy" value
        TRACE();
        auto checker = [&]() {
            TRACE();
            auto smart_origin = locked_ref(this->smart_origin);
            TRACE();
            return smart_origin->count(smart_addr);
        };
        TRACE();
        while (!checker()) {
            s_blockchain.wait_for_block();
            update_smart_caches();
        }
    }
    TRACE();
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
    return;
}

void
APIHandler::PoolListGet(api::PoolListGetResult& _return,
                        const int64_t offset,
                        const int64_t const_limit)
{
    ////////////////////////std::cout << "PoolListGet: " << offset << ", " <<
    /// const_limit << std::endl;

    if (offset > 100)
        const_cast<int64_t&>(offset) = 100;
    if (const_limit > 100)
        const_cast<int64_t&>(const_limit) = 100;

    _return.pools.reserve(const_limit);

    csdb::PoolHash hash = s_blockchain.getLastHash();

    size_t lastCount = 0;
    csdb::Pool pool; // = s_blockchain->loadBlock(hash/*, lastCount*/);

    uint64_t sequence = s_blockchain.getSize();

    const uint64_t lower =
      sequence - std::min(sequence, (uint64_t)(offset + const_limit));
    for (uint64_t it = sequence; it > lower; --it) {
        auto cch = poolCache.find(hash);

        if (cch == poolCache.end()) {
            pool = s_blockchain.loadBlock(hash /*, lastCount*/);
            api::Pool apiPool = convertPool(pool);

            if (it <= sequence - std::min(sequence, (uint64_t)offset)) {
                // apiPool.transactionsCount = lastCount;
                _return.pools.push_back(apiPool);
            }
            lastCount = 0;

            poolCache.insert(cch, std::make_pair(hash, apiPool));
            hash = pool.previous_hash();
        } else {
            _return.pools.push_back(cch->second);
            hash = csdb::PoolHash::from_string(cch->second.prevHash);
        }
    }
}

void
APIHandler::PoolTransactionsGet(PoolTransactionsGetResult& _return,
                                const PoolHash& hash,
                                const int64_t offset,
                                const int64_t limit)
{
    Log("PoolTransactionsGet");
    const csdb::PoolHash poolHash = csdb::PoolHash::from_string(hash);
    const csdb::Pool& pool = s_blockchain.loadBlock(poolHash);

    if (pool.is_valid()) {
        _return.transactions = extractTransactions(pool, limit, offset);
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
APIHandler::PoolInfoGet(PoolInfoGetResult& _return,
                        const PoolHash& hash,
                        const int64_t index)
{
    Log("PoolInfoGet");

    const csdb::PoolHash poolHash = csdb::PoolHash::from_string(hash);
    const csdb::Pool pool = s_blockchain.loadBlock(poolHash);
    _return.isFound = pool.is_valid();

    if (_return.isFound) {
        _return.pool = convertPool(poolHash);
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
APIHandler::StatsGet(api::StatsGetResult& _return)
{
#ifndef FOREVER_ALONE
    Log("StatsGet");
#endif

    csstats::StatsPerPeriod stats = this->stats.getStats();

    for (auto& s : stats) {
        api::PeriodStats ps = {};
        ps.periodDuration = s.periodSec;
        ps.poolsCount = s.poolsCount;
        ps.transactionsCount = s.transactionsCount;
        ps.smartContractsCount = s.smartContractsCount;

        for (auto& t : s.balancePerCurrency) {
            api::CumulativeAmount amount;
            amount.integral = t.second.integral;
            amount.fraction = t.second.fraction;
            ps.balancePerCurrency[t.first] = amount;
        }

        _return.stats.push_back(ps);
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
APIHandler::SmartContractGet(api::SmartContractGetResult& _return,
                             const api::Address& address)
{
    Log("SmartContractGet");

    // std::cerr << "Input address: " << address << std::endl;

    update_smart_caches();

    auto trid = [&]() -> decltype(auto) {
        TRACE();
        auto smart_origin = locked_ref(this->smart_origin);
        TRACE();
        return (*smart_origin)[BlockChain::getAddressFromKey(address)];
    }();
    auto tr = s_blockchain.loadTransaction(trid);
    _return.smartContract = fetch_smart_body(tr);

    SetResponseStatus(_return.status,
                      !_return.smartContract.address.empty()
                        ? APIRequestStatusType::SUCCESS
                        : APIRequestStatusType::FAILURE);
    return;
}

void
APIHandler::update_smart_caches()
{
    std::map<csdb::Address, std::list<csdb::TransactionID>::iterator> poss;
    std::set<csdb::Address> state_updated;
    TRACE();
    auto curr_ph = s_blockchain.getLastHash();
    auto curr_size = s_blockchain.getSize();
    TRACE();

    auto deployed_by_creator = locked_ref(this->deployed_by_creator);
    TRACE();
    auto smart_origin = locked_ref(this->smart_origin);
    TRACE();
    auto smart_state = locked_ref(this->smart_state);
    TRACE();
    auto smart_last_trxn = locked_ref(this->smart_last_trxn);
    TRACE();

    while (curr_size != last_seen_contract_block) {
        /*std::cerr << "curr_ph: " << curr_ph.to_string() << std::endl
                  << "last_seen_contract_block: "
                  << last_seen_contract_block.to_string() << std::endl;*/
        auto p = s_blockchain.loadBlock(curr_ph);
        // std::cerr << "Pool size: " << p.transactions_count() << std::endl;

        auto&& trs = p.transactions();
        for (auto i_tr = trs.rbegin(); i_tr != trs.rend(); ++i_tr) {
            auto&& tr = *i_tr;
            if (!is_smart(tr)) {
                continue;
            }
            auto smart = fetch_smart(tr);
            auto address = tr.target();
            if (is_smart_deploy(smart)) {
                {
                    (*smart_origin)[address] = tr.id();
                }
                {
                    auto& targetList = (*deployed_by_creator)[tr.source()];
                    decltype(poss.begin()) p;
                    std::tie(p, std::ignore) = poss.insert(
                      std::make_pair(tr.source(), targetList.begin()));
                    targetList.insert(p->second, tr.id());
                }
            }
            if (!state_updated.count(address)) {
                TRACE();
                auto target_cell = locked_ref((*smart_state)[address]);
                TRACE();
                *target_cell = tr.user_field(1).value<std::string>();
                state_updated.insert(address);
            } else {
                TRACE();
            }
            (*smart_last_trxn)[address].second = tr.id();
        }
        curr_ph = p.previous_hash();
        ++last_seen_contract_block;
    }
    TRACE();
}

template<typename Mapper>
void
APIHandler::get_mapped_deployer_smart(
  const csdb::Address& deployer,
  Mapper mapper,
  std::vector<decltype(mapper(api::SmartContract()))>& out)
{
    update_smart_caches();

    TRACE();
    auto deployed_by_creator = locked_ref(this->deployed_by_creator);
    TRACE();

    for (auto& trid : (*deployed_by_creator)[deployer]) {
        auto tr = s_blockchain.loadTransaction(trid);
        auto smart = fetch_smart_body(tr);
        out.push_back(mapper(smart));
    }
}

void
APIHandler::SmartContractsListGet(api::SmartContractsListGetResult& _return,
                                  const api::Address& deployer)
{
    Log("SmartContractsListGet");

    TRACE();

    csdb::Address addr = BlockChain::getAddressFromKey(deployer);

    // std::cerr << "Input address: " << deployer << std::endl;

    TRACE();

    get_mapped_deployer_smart(
      addr,
      [](const api::SmartContract& smart) { return smart; },
      _return.smartContractsList);

    TRACE();

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);

    //////////_return.printTo(std::cerr << "SmartContractListGetResult:" <<
    /// std::endl);
    //////////std::cerr << std::endl;
}

void
APIHandler::SmartContractAddressesListGet(
  api::SmartContractAddressesListGetResult& _return,
  const api::Address& deployer)
{
    Log("SmartContractAddressesListGet");

    csdb::Address addr = BlockChain::getAddressFromKey(deployer);

    get_mapped_deployer_smart(
      addr,
      [](const SmartContract& sc) { return sc.address; },
      _return.addressesList);

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
APIHandler::GetLastHash(api::PoolHash& _return)
{
    _return = s_blockchain.getLastHash().to_string();
    return;
}

void
APIHandler::PoolListGetStable(api::PoolListGetResult& _return,
                              const api::PoolHash& hash,
                              const int64_t limit)
{
    ////////////////////////std::cout << "PoolListGet: " << offset << ", " <<
    /// const_limit << std::endl;

    csdb::PoolHash cur_hash = csdb::PoolHash::from_string(hash);

    if (limit > 100)
        const_cast<int64_t&>(limit) = 100;

    _return.pools.reserve(limit);

    size_t lastCount = 0;
    csdb::Pool pool; // = s_blockchain->loadBlock(hash/*, lastCount*/);

    for (int pools_left = limit; pools_left && !cur_hash.is_empty();
         --pools_left) {
        auto cch = poolCache.find(cur_hash);

        if (cch == poolCache.end()) {
            pool = s_blockchain.loadBlock(cur_hash);
            api::Pool apiPool = convertPool(pool);
            _return.pools.push_back(apiPool);
            lastCount = 0;

            poolCache.insert(cch, std::make_pair(cur_hash, apiPool));
            cur_hash = pool.previous_hash();
        } else {
            _return.pools.push_back(cch->second);
            cur_hash = csdb::PoolHash::from_string(cch->second.prevHash);
        }
    }
}

void
APIHandler::WaitForSmartTransaction(api::TransactionId& _return,
                                    const api::Address& smart_public)
{
    decltype(smart_last_trxn)::LockedType::iterator it;
    {
        // bool present;
        TRACE();
        auto smart_last_trxn = locked_ref(this->smart_last_trxn);
        TRACE();
        std::tie(it, std::ignore) = smart_last_trxn->emplace(
          BlockChain::getAddressFromKey(smart_public),
          std::make_pair(0, csdb::TransactionID()));
        // if (present) {
        //    auto&& entry = it->second;
        //    auto&& ret_val = entry.second;
        //    if (ret_val.is_valid()) {
        //        _return = convert_transaction_id(ret_val);
        //        if (entry.first == 0) {
        //            smart_last_trxn.erase(it);
        //        }
        //        return;
        //    } else {
        //        ++entry.first;
        //    }
        //}
    }
    auto&& entry = it->second;
    ++entry.first;
    while (true) {
        {
            TRACE();
            auto smart_last_trxn = locked_ref(this->smart_last_trxn);
            TRACE();
            if (entry.second.is_valid()) {
                _return = convert_transaction_id(entry.second);
                if (--entry.first == 0) {
                    smart_last_trxn->erase(it);
                }
                return;
            }
        }
        s_blockchain.wait_for_block();
        update_smart_caches();
    }
}

void
APIHandler::SmartContractsAllListGet(SmartContractsListGetResult& _return,
                                     const int64_t _offset,
                                     const int64_t _limit)
{
    Log("SmartContractsAllListGet");

    update_smart_caches();

    int64_t offset = _offset;
    int64_t limit = _limit;

    TRACE();
    auto smart_origin = locked_ref(this->smart_origin);
    TRACE();

    for (auto p : *smart_origin) {
        if (offset) {
            --offset;
        } else if (limit) {
            auto trid = p.second;
            auto tr = s_blockchain.loadTransaction(trid);
            _return.smartContractsList.push_back(fetch_smart_body(tr));
            --limit;
        } else
            break;
    }

    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}
