#pragma once

#include <csnode/blockchain.hpp>

#include <deque>
#include <mutex>
#include <queue>

#include <API.h>
#include <Solver/Solver.hpp>
//#include <csconnector/csconnector.h>
#include <csstats.h>

#include <ContractExecutor.h>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>

#include <csnode/threading.hpp>

class APIHandlerBase
{
  public:
    enum class APIRequestStatusType : uint8_t
    {
        SUCCESS,
        FAILURE,
        NOT_IMPLEMENTED,
        NOT_FOUND,
        MAX
    };

    static void SetResponseStatus(api::APIResponse& response,
                                  APIRequestStatusType status,
                                  const std::string& details = "");
    static void SetResponseStatus(api::APIResponse& response,
                                  bool commandWasHandled);
};

struct APIHandlerInterface
  : public api::APINull
  , public APIHandlerBase
{};

namespace api {
namespace custom {
class APIProcessor;
}

class APIFaker : public APINull
{
  public:
    APIFaker(BlockChain&, Credits::Solver&) {}
};
//
//#ifndef FAKE_API_HANDLING
//class APIHandler;
//using APIHandlerImpl = APIHandler;
//#else
//using APIHandlerImpl = APIFaker;
//#endif

class APIHandler : public APIHandlerInterface
{
  public:
    APIHandler(BlockChain& blockchain, Credits::Solver& _solver);
    ~APIHandler() override;

    APIHandler(const APIHandler&) = delete;

    void WalletDataGet(api::WalletDataGetResult& _return,
                       const api::Address& address) override;
    void WalletIdGet(api::WalletIdGetResult& _return, const Address& address) override;
    void WalletTransactionsCountGet(api::WalletTransactionsCountGetResult& _return, const Address& address) override;
    void WalletBalanceGet(api::WalletBalanceGetResult& _return, const Address& address) override;

    void TransactionGet(api::TransactionGetResult& _return,
                        const api::TransactionId& transactionId) override;
    void TransactionsGet(api::TransactionsGetResult& _return,
                         const api::Address& address,
                         const int64_t offset,
                         const int64_t limit) override;
    void TransactionFlow(api::TransactionFlowResult& _return,
                         const api::Transaction& transaction) override;

    // Get list of pools from last one (head pool) to the first one.
    void PoolListGet(api::PoolListGetResult& _return,
                     const int64_t offset,
                     const int64_t limit) override;

    // Get pool info by pool hash. Starts looking from last one (head pool).
    void PoolInfoGet(api::PoolInfoGetResult& _return,
                     const api::PoolHash& hash,
                     const int64_t index) override;
    void PoolTransactionsGet(api::PoolTransactionsGetResult& _return,
                             const api::PoolHash& hash,
                             const int64_t offset,
                             const int64_t limit) override;
    void StatsGet(api::StatsGetResult& _return) override;

    void SmartContractGet(api::SmartContractGetResult& _return,
                          const api::Address& address) override;

    void SmartContractsListGet(api::SmartContractsListGetResult& _return,
                               const api::Address& deployer) override;

    void SmartContractAddressesListGet(
      api::SmartContractAddressesListGetResult& _return,
      const api::Address& deployer) override;

    void GetLastHash(api::PoolHash& _return) override;
    void PoolListGetStable(api::PoolListGetResult& _return,
                           const api::PoolHash& hash,
                           const int64_t limit) override;

    void WaitForSmartTransaction(api::TransactionId& _return,
                                 const api::Address& smart_public) override;

    void SmartContractsAllListGet(api::SmartContractsListGetResult& _return,
                                  const int64_t offset,
                                  const int64_t limit) override;

    void WaitForBlock(PoolHash& _return, const PoolHash& obsolete) override;

  private:
    BlockChain& s_blockchain;

    api::Pool convertPool(const csdb::Pool& pool);

    api::Pool convertPool(const csdb::PoolHash& poolHash);

    Credits::Solver& solver;
    csstats::csstats stats;

    ::apache::thrift::stdcxx::shared_ptr<
      ::apache::thrift::transport::TTransport>
      executor_transport;

    ::executor::ContractExecutorConcurrentClient executor;

    Credits::SpinLockable<std::map<csdb::Address, csdb::TransactionID>>
      smart_origin;

    using smart_state_entry = Credits::worker_queue<std::string>;

    Credits::SpinLockable<std::map<csdb::Address, smart_state_entry>>
      smart_state;

    struct smart_trxns_queue
    {
        Credits::spinlock lock;
        std::condition_variable_any new_trxn_cv{};
        size_t awaiter_num{ 0 };
        std::deque<csdb::TransactionID> trid_queue{};
    };
    Credits::SpinLockable<std::map<csdb::Address, smart_trxns_queue>>
      smart_last_trxn;

    Credits::SpinLockable<
      std::map<csdb::Address, std::vector<csdb::TransactionID>>>
      deployed_by_creator;

    struct PendingSmartTransactions
    {
        std::queue<csdb::Transaction> queue;
        csdb::PoolHash last_pull_hash{};
    };
    Credits::SpinLockable<PendingSmartTransactions> pending_smart_transactions;

    template<typename Mapper>
    void get_mapped_deployer_smart(
      const csdb::Address& deployer,
      Mapper mapper,
      std::vector<decltype(mapper(api::SmartContract()))>& out);

    bool update_smart_caches_once(const csdb::PoolHash&, bool = false);

    std::map<csdb::PoolHash, api::Pool> poolCache;

    std::atomic_flag state_updater_running = ATOMIC_FLAG_INIT;
    std::thread state_updater;

    friend class api::custom::APIProcessor;

    ::csdb::Transaction make_transaction(const ::api::Transaction&);
    void dumb_transaction_flow(api::TransactionFlowResult& _return,
                               const ::api::Transaction&);
    void smart_transaction_flow(api::TransactionFlowResult& _return,
                                            const ::api::Transaction&);

    std::map<std::string, Credits::worker_queue<std::tuple<>>> work_queues;
};

class SequentialProcessorFactory;

namespace custom {
class APIProcessor : public api::APIProcessor
{
  public:
    APIProcessor(::apache::thrift::stdcxx::shared_ptr<APIHandler> iface);
    Credits::sweet_spot ss;

  protected:
    bool dispatchCall(::apache::thrift::protocol::TProtocol* iprot,
                      ::apache::thrift::protocol::TProtocol* oprot,
                      const std::string& fname,
                      int32_t seqid,
                      void* callContext) override;

  private:
    friend class ::api::SequentialProcessorFactory;
};
}

class SequentialProcessorFactory : public ::apache::thrift::TProcessorFactory
{
  public:
    SequentialProcessorFactory(api::custom::APIProcessor& processor)
      : processor_(processor)
    {}

    ::apache::thrift::stdcxx::shared_ptr<::apache::thrift::TProcessor>
    getProcessor(const ::apache::thrift::TConnectionInfo& ci) override
    {
        (void)ci;
        // TRACE("");
        processor_.ss.occupy();
        // TRACE("");
        auto deleter = [](api::custom::APIProcessor* p) {
            // TRACE("");
            p->ss.leave();
            // TRACE("");
        };
        return ::apache::thrift::stdcxx::shared_ptr<api::custom::APIProcessor>(
          &processor_, deleter);
    }

  private:
    api::custom::APIProcessor& processor_;
};
}

api::SealedTransaction
convertTransaction(const csdb::Transaction& transaction);

template<typename T>
T
deserialize(std::string&& s)
{

  using namespace ::apache;

  // https://stackoverflow.com/a/16261758/2016154
  static_assert(
    CHAR_BIT == 8 && std::is_same<std::uint8_t, unsigned char>::value,
    "This code requires std::uint8_t to be implemented as unsigned char.");

  auto buffer = thrift::stdcxx::make_shared<thrift::transport::TMemoryBuffer>(
    reinterpret_cast<uint8_t*>(&(s[0])), (uint32_t)s.size());
  thrift::protocol::TBinaryProtocol proto(buffer);
  T sc;
  sc.read(&proto);
  return sc;
}

template<typename T>
std::string
serialize(const T& sc)
{
  using namespace ::apache;

  auto buffer = thrift::stdcxx::make_shared<thrift::transport::TMemoryBuffer>();
  thrift::protocol::TBinaryProtocol proto(buffer);
  sc.write(&proto);
  return buffer->getBufferAsString();
}

bool
is_deploy_transaction(const csdb::Transaction& tr);