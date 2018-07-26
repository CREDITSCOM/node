#pragma once

#include <csnode/blockchain.hpp>
#include <mutex>

#include <API.h>
#include <Solver/ISolver.hpp>
#include <csconnector/csconnector.h>
#include <csstats.h>

#include <ContractExecutor.h>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>

class APIHandlerBase
{
  public:
    enum class APIRequestStatusType : uint8_t
    {
        SUCCESS,
        FAILURE,
        NOT_IMPLEMENTED,
        MAX
    };

    static void SetResponseStatus(api::APIResponse& response,
                                  APIRequestStatusType status,
                                  const std::string& details = "");
    static void SetResponseStatus(api::APIResponse& response,
                                  bool commandWasHandled);
};

struct APIHandlerInterface
  : virtual public api::APIIf
  , public APIHandlerBase
{};

using namespace ::apache;

class APIHandler : public APIHandlerInterface
{
  public:
    APIHandler(BlockChain& blockchain, Credits::ISolver& _solver);
    ~APIHandler() override = default;

    void BalanceGet(api::BalanceGetResult& _return,
                    const api::Address& address,
                    const api::Currency& currency) override;

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
                             const int64_t index,
                             const int64_t offset,
                             const int64_t limit) override;
    void StatsGet(api::StatsGetResult& _return) override;

    void NodesInfoGet(api::NodesInfoGetResult& _return) override;

    void SmartContractGet(api::SmartContractGetResult& _return,
                          const api::Address& address) override;

    void SmartContractsListGet(api::SmartContractsListGetResult& _return,
                               const api::Address& deployer) override;

    void SmartContractAddressesListGet(
      api::SmartContractAddressesListGetResult& _return,
      const api::Address& deployer) override;

  private:
    bool GetTransaction(const api::TransactionId& transactionId,
                        api::Transaction& transaction);
    BlockChain& s_blockchain;

    api::Pool convertPool(const csdb::Pool& pool);

    api::Pool convertPool(const csdb::PoolHash& poolHash);

    api::SmartContract convertStringToContract(const std::string& data);

    void GetSmartContractAddress(const std::string& data,
                                 std::string& smartContractAddress);

    Credits::ISolver& solver;

    csstats::csstats stats;

    ::apache::thrift::stdcxx::shared_ptr<
      ::apache::thrift::transport::TTransport>
      executor_transport;

    ::executor::ContractExecutorClient executor;

    template<typename MapperT, typename Set>
    void map_smart_contracts_of_deployer(const csdb::Address& deployer,
                                         MapperT mapper,
                                         Set& result);

    template<typename Filter>
    api::SmartContract get_smart_contract(api::SmartContract& slim_contract,
                                          Filter f);

    std::map<decltype(api::SmartContract::address), csdb::TransactionID>
      smart_origin, smart_state;

    std::map<csdb::Address, std::list<csdb::TransactionID>> deployed_by_creator;

    csdb::PoolHash last_seen_contract_block;

    template<typename Mapper>
    void get_mapped_deployer_smart(
      const csdb::Address& deployer,
      Mapper mapper,
      std::vector<decltype(mapper(api::SmartContract()))>& out);

    void update_smart_caches();

    std::map<csdb::PoolHash, api::Pool> poolCache;
};
