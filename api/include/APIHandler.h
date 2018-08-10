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

template<typename T>
struct LockedRef;

template<typename T>
struct Lockable
{
    using LockedType = T;

    template<typename... Args>
    Lockable(Args&&... args)
      : t(std::forward<Args>(args)...)
    {}

  private:
    std::mutex m;
    T t;

    friend struct LockedRef<T>;
};

template<typename T>
struct LockedRef
{
  private:
    Lockable<T> *l;

  public:
    LockedRef(Lockable<T>& l)
      : l(&l)
    {
        this->l->m.lock();
    }
    ~LockedRef() { if (l) l->m.unlock(); }

    LockedRef(const LockedRef&) = delete;
	LockedRef(LockedRef&& other)
		: l(other.l)
	{
            other.l = nullptr;
	}

        operator T*()
    {
        return &(l->t);
    }
    T* operator->() { return *this; }
    T& operator*() { return l->t; }
};

template<typename T>
struct SpinLockedRef;

template<typename T>
struct SpinLockable
{
    using LockedType = T;

    template<typename... Args>
    SpinLockable(Args&&... args)
      : t(std::forward<Args>(args)...)
    {}

  private:
    std::atomic_flag af;
    T t;

    friend struct SpinLockedRef<T>;
};

template<typename T>
struct SpinLockedRef
{
  private:
    SpinLockable<T> *l;

  public:
    SpinLockedRef(SpinLockable<T>& l)
      : l(&l)
    {
        while (this->l->af.test_and_set(std::memory_order_acquire))
            ;
    }
    ~SpinLockedRef() { if (l) l->af.clear(std::memory_order_release); }

    SpinLockedRef(const SpinLockedRef&) = delete;
    SpinLockedRef(SpinLockedRef&& other)
      : l(other.l)
    {
        other.l = nullptr;
    }

    operator T*() { return &(l->t); }
    T* operator->() { return *this; }
    T& operator*() { return l->t; }
};

template<typename T>
LockedRef<T>
locked_ref(Lockable<T>& l)
{
    LockedRef<T> r(l);
    return r;
}

template<typename T>
SpinLockedRef<T>
locked_ref(SpinLockable<T>& l)
{
    return SpinLockedRef<T>(l);
}

using namespace ::apache;

class APIHandler : public APIHandlerInterface
{
  public:
    APIHandler(BlockChain& blockchain, Credits::ISolver& _solver);
    ~APIHandler() override = default;

    APIHandler(const APIHandler&) = delete;

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

  private:
    BlockChain& s_blockchain;

    api::Pool convertPool(const csdb::Pool& pool);

    api::Pool convertPool(const csdb::PoolHash& poolHash);

    Credits::ISolver& solver;
    csstats::csstats stats;

    ::apache::thrift::stdcxx::shared_ptr<
      ::apache::thrift::transport::TTransport>
      executor_transport;

    ::executor::ContractExecutorClient executor;

    Lockable<std::map<csdb::Address, csdb::TransactionID>> smart_origin;
    Lockable<std::map<csdb::Address, SpinLockable<std::string>>> smart_state;
    Lockable<
      std::map<csdb::Address, std::pair<std::atomic_int, csdb::TransactionID>>>
      smart_last_trxn;

    Lockable<std::map<csdb::Address, std::list<csdb::TransactionID>>>
      deployed_by_creator;

    size_t last_seen_contract_block;

    template<typename Mapper>
    void get_mapped_deployer_smart(
      const csdb::Address& deployer,
      Mapper mapper,
      std::vector<decltype(mapper(api::SmartContract()))>& out);

    void update_smart_caches();

    std::map<csdb::PoolHash, api::Pool> poolCache;
};
