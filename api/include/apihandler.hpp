#ifndef APIHANDLER_HPP
#define APIHANDLER_HPP

#if defined(_MSC_VER)
#pragma warning(push)
// 4706 - assignment within conditional expression
// 4373 - 'api::APIHandler::TokenTransfersListGet': virtual function overrides 'api::APINull::TokenTransfersListGet',
//         previous versions of the compiler did not override when parameters only differed by const/volatile qualifiers
#pragma warning(disable: 4706 4373)
#endif

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>

#include <csnode/blockchain.hpp>

#include <API.h>
#include <executor_types.h>
#include <general_types.h>

#include <csstats.hpp>
#include <deque>
#include <queue>

#include <client/params.hpp>
#include <lib/system/concurrent.hpp>

#include "tokens.hpp"

#include <APIEXEC.h>
#include <thrift/transport/TSocket.h>
#include <optional>

#include <csdb/currency.hpp>

class APIHandlerBase {
public:
  enum class APIRequestStatusType : uint8_t {
    SUCCESS,
    FAILURE,
    NOT_IMPLEMENTED,
    NOT_FOUND,
    MAX
  };

  static void SetResponseStatus(general::APIResponse& response, APIRequestStatusType status,
                                const std::string& details = "");
  static void SetResponseStatus(general::APIResponse& response, bool commandWasHandled);
};

struct APIHandlerInterface : public api::APINull, public APIHandlerBase {};

template <typename T>
T deserialize(std::string&& s) {
  using namespace ::apache;

  // https://stackoverflow.com/a/16261758/2016154
  static_assert(CHAR_BIT == 8 && std::is_same<std::uint8_t, unsigned char>::value,
    "This code requires std::uint8_t to be implemented as unsigned char.");

  const auto buffer = thrift::stdcxx::make_shared<thrift::transport::TMemoryBuffer>(reinterpret_cast<uint8_t*>(&(s[0])),
    static_cast<uint32_t>(s.size()));
  thrift::protocol::TBinaryProtocol proto(buffer);
  T sc;
  sc.read(&proto);
  return sc;
}

template <typename T>
std::string serialize(const T& sc) {
  using namespace ::apache;

  auto buffer = thrift::stdcxx::make_shared<thrift::transport::TMemoryBuffer>();
  thrift::protocol::TBinaryProtocol proto(buffer);
  sc.write(&proto);
  return buffer->getBufferAsString();
}

namespace cs {
class SolverCore;
}

namespace csconnector {
struct Config;
}

namespace executor {
class APIResponse;
class ContractExecutorConcurrentClient;
}

namespace executor {
  class Executor {
  public: // wrappers
    void executeByteCode(executor::ExecuteByteCodeResult& resp, const std::string& address, const std::string& smart_address,
      const std::vector<general::ByteCodeObject>& code, const std::string& state, const std::string& method,
      const std::vector<general::Variant>& params, const int64_t &timeout) {
      static std::mutex m;
      std::lock_guard lk(m); // temporary solution

      if (!code.empty()) {
        if (!connect()) return;
        executor::SmartContractBinary smartContractBinary;
        smartContractBinary.contractAddress = smart_address;
        smartContractBinary.byteCodeObjects = code;
        smartContractBinary.contractState   = state;
        smartContractBinary.stateCanModify  = 1; // solver_->smart_contracts().is_contract_locked(csdb::Address::from_???(smart_address)) ? 1 : 0;

        const auto acceess_id = generateAccessId();
        ++execCount_;
        origExecutor_->executeByteCode(resp, acceess_id, address, smartContractBinary, method, params, timeout);
        --execCount_;
        deleteAccessId(acceess_id);
        disconnect();
      }
    }

    void executeByteCodeMultiple(ExecuteByteCodeMultipleResult& _return, const ::general::Address& initiatorAddress, const SmartContractBinary& invokedContract, const std::string& method, const std::vector<std::vector< ::general::Variant> > & params, const int64_t executionTime) {
      if (!connect()) return;
      const auto acceess_id = generateAccessId();
      ++execCount_;
      origExecutor_->executeByteCodeMultiple(_return, acceess_id, initiatorAddress, invokedContract, method, params, executionTime);
      --execCount_;
      deleteAccessId(acceess_id);
      disconnect();
    }

    void getContractMethods(GetContractMethodsResult& _return, const std::vector< ::general::ByteCodeObject> & byteCodeObjects) {
      if (!connect()) return;
      origExecutor_->getContractMethods(_return, byteCodeObjects);
      disconnect();
    }

    void getContractVariables(GetContractVariablesResult& _return, const std::vector<::general::ByteCodeObject> & byteCodeObjects, const std::string& contractState) {
      if (!connect()) return;
      origExecutor_->getContractVariables(_return, byteCodeObjects, contractState);
      disconnect();
    }

    void compileSourceCode(CompileSourceCodeResult& _return, const std::string& sourceCode) {
      if (!connect()) return;
      origExecutor_->compileSourceCode(_return, sourceCode);
      disconnect();
    }

  public:
    static Executor& getInstance(const BlockChain &p_blockchain, const int p_exec_port) { // singlton
      static Executor executor(p_blockchain, p_exec_port);
      return  executor;
    }

    std::optional<cs::Sequence> getSequence(const general::AccessID& accessId) {
      std::shared_lock slk(mtx_);
      if (auto it = accessSequence_.find(accessId); it != accessSequence_.end())
        return std::make_optional(it->second);
      return std::nullopt;
    };

    std::optional<csdb::TransactionID> getDeployTrxn(const csdb::Address& p_address) {
      std::shared_lock slk(mtx_);
      if (const auto it = deployTrxns_.find(p_address); it != deployTrxns_.end())
        return std::make_optional(it->second);
      return std::nullopt;
    }

    void updateDeployTrxns(const csdb::Address& p_address, const csdb::TransactionID& p_trxnsId) {
      std::lock_guard lk(mtx_);
      deployTrxns_[p_address] = p_trxnsId;
    }

    uint64_t generateAccessId() {
      std::lock_guard lk(mtx_);
      ++lastAccessId_;
      accessSequence_[lastAccessId_] = blockchain_.getLastSequence();
      return lastAccessId_;
    }

    void deleteAccessId(const general::AccessID& p_access_id) {
      std::lock_guard lk(mtx_);
      accessSequence_.erase(p_access_id);
    }

    void setLastState(const csdb::Address& p_address, const std::string& p_state) {
      std::lock_guard lk(mtx_);
      lastState_[p_address] = p_state;
    }

    std::optional<std::string> getState(const csdb::Address& p_address) {
      std::shared_lock slk(mtx_);
      if (const auto it_last_state = lastState_.find(p_address); it_last_state != lastState_.end())
        return std::make_optional(it_last_state->second);
      return std::nullopt;
    }

    void updateCacheLastStates(const csdb::Address& p_address, const cs::Sequence& sequence, const std::string& state) {
      std::lock_guard lk(mtx_);
      if (execCount_)
        (cacheLastStates_[p_address])[sequence] = state;
      else if (cacheLastStates_.size())
        cacheLastStates_.clear();
    }

    std::optional<std::string> getAccessState(const general::AccessID& p_access_id, const csdb::Address& p_address) {
      std::shared_lock slk(mtx_);
      const auto access_sequence = getSequence(p_access_id);
      if (const auto unmap_states_it = cacheLastStates_.find(p_address); unmap_states_it != cacheLastStates_.end()) {
        std::pair<cs::Sequence, std::string> prev_seq_state{};
        for (const auto &[curr_seq, curr_state] : unmap_states_it->second) {
          if (curr_seq > access_sequence)
            return prev_seq_state.first ? std::make_optional<std::string>(prev_seq_state.second) : std::nullopt;
          prev_seq_state = { curr_seq , curr_state };
        }
      }
      auto opt_last_sate = getState(p_address);
      return opt_last_sate.has_value() ? std::make_optional<std::string>(opt_last_sate.value()) : std::nullopt;
    }

    struct ExecuteResult {
      std::string newState;
      std::map<csdb::Address, std::string> states;
      std::vector<csdb::Transaction> trxns;
      csdb::Amount fee;
      general::Variant retValue;
    };

    void addInnerSendTransaction(const general::AccessID &accessId, const csdb::Transaction &transaction) {
      std::lock_guard lk(mtx_);
      innerSendTransactions_[accessId].push_back(transaction);
    }

    std::optional<std::vector<csdb::Transaction>> getInnerSendTransactions(const general::AccessID &accessId) {
      std::shared_lock slk(mtx_);
      if (const auto it = innerSendTransactions_.find(accessId); it != innerSendTransactions_.end())
        return std::make_optional<std::vector<csdb::Transaction>>(it->second);
      return std::nullopt;
    }

    void deleteInnerSendTransactions(const general::AccessID &accessId) {
      std::lock_guard lk(mtx_);
      innerSendTransactions_.erase(accessId);
    }

    bool isDeploy(const csdb::Transaction& trxn) {
      if (!trxn.user_field(0).is_valid()) {
        return false;
      }
      const auto sci = deserialize<api::SmartContractInvocation>(trxn.user_field(0).value<std::string>());
      if (sci.method.empty())
        return true;
      return false;
    }

    std::optional<ExecuteResult> executeTransaction(const csdb::Pool& pool, const uint64_t& offsetTrx, const csdb::Amount& feeLimit) {
      csunused(feeLimit);
      static std::mutex m;
      std::lock_guard lk(m); // temporary solution

      auto smartTrxn = *(pool.transactions().begin() + offsetTrx);

      csdb::Transaction deployTrxn;
      if (!isDeploy(smartTrxn)) { // execute
        const auto optDeployId = getDeployTrxn(blockchain_.get_addr_by_type(smartTrxn.target(), BlockChain::ADDR_TYPE::PUBLIC_KEY));
        if (!optDeployId.has_value())
          return std::nullopt;
        deployTrxn = blockchain_.loadTransaction(optDeployId.value());
      }
      else
        deployTrxn = smartTrxn;
      const auto sci = deserialize<api::SmartContractInvocation>(deployTrxn.user_field(0).value<std::string>());

      ExecuteByteCodeResult resp;        
      executor::SmartContractBinary smartContractBinary;
      smartContractBinary.contractAddress = smartTrxn.target().to_api_addr();
      smartContractBinary.byteCodeObjects = sci.smartContractDeploy.byteCodeObjects;
      smartContractBinary.contractState   = sci.smartContractDeploy.hashState;
      smartContractBinary.stateCanModify  = 1; // solver_->smart_contracts().is_contract_locked(executeTrxn_it->target()) : 1 : 0;

      std::string method;
      std::vector<general::Variant> params;
      if (!smartTrxn.user_field(0).is_valid() && smartTrxn.amount().to_double()) { // payable
        method = "payable";
        general::Variant var;
        var.__set_v_string(smartTrxn.amount().to_string());
        params.emplace_back(var);
        var.__set_v_string(smartTrxn.currency().to_string());
        params.emplace_back(var);
      }
      else {
        method = sci.method;
        params = sci.params;
      }

      if (!connect()) return std::nullopt;
      const auto acceess_id = generateAccessId();
      ++execCount_;
      const auto timeBeg = std::chrono::system_clock::now();
      origExecutor_->executeByteCode(resp, acceess_id, smartTrxn.source().to_api_addr(), smartContractBinary, sci.method, sci.params, 1000);
      const auto timeExecute = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - timeBeg).count();
      --execCount_;
      deleteAccessId(acceess_id);
      disconnect();

      const auto optInnerTransactions = getInnerSendTransactions(acceess_id);

      ExecuteResult res;
      if (optInnerTransactions.has_value())
        res.trxns = optInnerTransactions.value();
      deleteInnerSendTransactions(acceess_id);
      static const double FEE_IN_SECOND = kMinFee * 4.0;
      res.fee = csdb::Amount(timeExecute * FEE_IN_SECOND);
      res.newState  = resp.invokedContractState;
      for (const auto &[itAddress, itState] : resp.externalContractsState) {
        const csdb::Address addr = BlockChain::getAddressFromKey(itAddress);
        res.states[addr] = itState;
      }
      res.retValue  = resp.ret_val;
      return res;
    }

    csdb::Transaction make_transaction(const api::Transaction& transaction) {
      csdb::Transaction send_transaction;
      const auto source = BlockChain::getAddressFromKey(transaction.source);
      const uint64_t WALLET_DENOM = csdb::Amount::AMOUNT_MAX_FRACTION;  // 1'000'000'000'000'000'000ull;
      send_transaction.set_amount(csdb::Amount(transaction.amount.integral, transaction.amount.fraction, WALLET_DENOM));
      BlockChain::WalletData wallData{};
      BlockChain::WalletId id{};

      if (!blockchain_.findWalletData(source, wallData, id))
      return csdb::Transaction{};

      send_transaction.set_currency(csdb::Currency(1));
      send_transaction.set_source(source);
      send_transaction.set_target(BlockChain::getAddressFromKey(transaction.target));
      send_transaction.set_max_fee(csdb::AmountCommission((uint16_t)transaction.fee.commission));
      send_transaction.set_innerID(transaction.id & 0x3fffffffffff);

      // TODO Change Thrift to avoid copy
      cs::Signature signature;
      if (transaction.signature.size() == signature.size())
        std::copy(transaction.signature.begin(), transaction.signature.end(), signature.begin());
      else
        signature.fill(0);
      send_transaction.set_signature(signature);
      return send_transaction;
    }

    bool isConnect() { return isConnect_; }

  private:
    explicit Executor(const BlockChain &p_blockchain, const int p_exec_port) :
      executorTransport_(new ::apache::thrift::transport::TBufferedTransport(
        ::apache::thrift::stdcxx::make_shared<::apache::thrift::transport::TSocket>("localhost", p_exec_port)))
      , origExecutor_(std::make_unique<executor::ContractExecutorConcurrentClient>(
        ::apache::thrift::stdcxx::make_shared<apache::thrift::protocol::TBinaryProtocol>(executorTransport_)))
      , blockchain_(p_blockchain)
    {
      std::thread th([&]() {
        while (true) {
          if (isConnect_) {
            static std::mutex mt;
            std::unique_lock ulk(mt);
            cvErrorConnect_.wait(ulk, [&] { return !isConnect_; });
          }

          try {
            static const int RECONNECT_TIME = 10;
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_TIME));
            executorTransport_->open();
            if (executorTransport_->isOpen())
              isConnect_ = true;
          }
          catch (...) {
            isConnect_ = false;
            continue;
          }
        }
      });
      th.detach();
    }

    bool connect() {
      try {
        if (!executorTransport_->isOpen()) {
          executorTransport_->open();
          isConnect_ = true;
        }
      }
      catch (...) {
        isConnect_ = false;
        cvErrorConnect_.notify_one();
        return false;
      }
      return true;
    }

    void disconnect() {
      executorTransport_->close();
    }

  private:
    const BlockChain& blockchain_;
    ::apache::thrift::stdcxx::shared_ptr<::apache::thrift::transport::TTransport> executorTransport_;
    std::unique_ptr<executor::ContractExecutorConcurrentClient> origExecutor_;

    general::AccessID lastAccessId_{};
    std::map<general::AccessID, cs::Sequence> accessSequence_;
    std::map<csdb::Address, csdb::TransactionID> deployTrxns_;
    std::map<csdb::Address, std::string> lastState_;
    std::map<csdb::Address, std::unordered_map<cs::Sequence, std::string>> cacheLastStates_;
    std::map<general::AccessID, std::vector<csdb::Transaction>> innerSendTransactions_;

    std::shared_mutex mtx_;
    std::atomic_size_t execCount_{ 0 };

    std::condition_variable cvErrorConnect_;
    std::atomic_bool isConnect_{ false };
  };
}
namespace apiexec {
  class APIEXECHandler : public APIEXECNull, public APIHandlerBase {
  public:
    explicit APIEXECHandler(BlockChain& blockchain, cs::SolverCore& _solver, executor::Executor& executor, const csconnector::Config& config);
    APIEXECHandler(const APIEXECHandler&) = delete;
    void GetSeed(apiexec::GetSeedResult &_return, const general::AccessID accessId) override;
    void SendTransaction(apiexec::SendTransactionResult &_return, const general::AccessID accessId, const api::Transaction &transaction) override;
    void WalletIdGet(api::WalletIdGetResult &_return, const general::AccessID accessId, const general::Address &address) override;
    void SmartContractGet(SmartContractGetResult &_return, const general::AccessID accessId, const general::Address &address) override;

    executor::Executor& getExecutor() const {
      return executor_;
    }

  private:
    executor::Executor  &executor_;
    BlockChain          &blockchain_;
    cs::SolverCore      &solver_;
  };
}

namespace api {
class APIFaker : public APINull {
public:
  APIFaker(BlockChain&, cs::SolverCore&) {
  }
};

class APIHandler : public APIHandlerInterface {
public:
  explicit APIHandler(BlockChain& blockchain, cs::SolverCore& _solver, executor::Executor& executor, const csconnector::Config& config);
  ~APIHandler() override;

  APIHandler(const APIHandler&) = delete;

  void WalletDataGet(api::WalletDataGetResult& _return, const general::Address& address) override;
  void WalletIdGet(api::WalletIdGetResult& _return, const general::Address& address) override;
  void WalletTransactionsCountGet(api::WalletTransactionsCountGetResult& _return, const general::Address& address) override;
  void WalletBalanceGet(api::WalletBalanceGetResult& _return, const general::Address& address) override;

  void TransactionGet(api::TransactionGetResult& _return, const api::TransactionId& transactionId) override;
  void TransactionsGet(api::TransactionsGetResult& _return, const general::Address& address, const int64_t offset,
                       const int64_t limit) override;
  void TransactionFlow(api::TransactionFlowResult& _return, const api::Transaction& transaction) override;

  // Get list of pools from last one (head pool) to the first one.
  void PoolListGet(api::PoolListGetResult& _return, const int64_t offset, const int64_t limit) override;

  // Get pool info by pool hash. Starts looking from last one (head pool).
  void PoolInfoGet(api::PoolInfoGetResult& _return, const api::PoolHash& hash, const int64_t index) override;
  void PoolTransactionsGet(api::PoolTransactionsGetResult& _return, const api::PoolHash& hash, const int64_t offset,
                           const int64_t limit) override;
  void StatsGet(api::StatsGetResult& _return) override;

  void SmartContractGet(api::SmartContractGetResult& _return, const general::Address& address) override;

  void SmartContractsListGet(api::SmartContractsListGetResult& _return, const general::Address& deployer) override;

  void SmartContractAddressesListGet(api::SmartContractAddressesListGetResult& _return,
                                     const general::Address& deployer) override;

  void GetLastHash(api::PoolHash& _return) override;
  void PoolListGetStable(api::PoolListGetResult& _return, const api::PoolHash& hash, const int64_t limit) override;

  void WaitForSmartTransaction(api::TransactionId& _return, const general::Address& smart_public) override;

  void SmartContractsAllListGet(api::SmartContractsListGetResult& _return, const int64_t offset,
                                const int64_t limit) override;

  void WaitForBlock(PoolHash& _return, const PoolHash& obsolete) override;

  void SmartMethodParamsGet(SmartMethodParamsGetResult& _return, const general::Address& address, const int64_t id) override;

  void TransactionsStateGet(TransactionsStateGetResult& _return, const general::Address& address,
                            const std::vector<int64_t>& v) override;

  void ContractAllMethodsGet(ContractAllMethodsGetResult& _return, const std::vector<::general::ByteCodeObject> & byteCodeObjects) override;

  ////////new
  void iterateOverTokenTransactions(const csdb::Address&, const std::function<bool(const csdb::Pool&, const csdb::Transaction&)>);
  ////////new
  api::SmartContractInvocation getSmartContract(const csdb::Address&, bool&);
  std::vector<general::ByteCodeObject> getSmartByteCode(const csdb::Address&, bool&);
  void SmartContractDataGet(api::SmartContractDataResult&, const general::Address&) override;
  void SmartContractCompile(api::SmartContractCompileResult&, const std::string&) override;

  void TokenBalancesGet(api::TokenBalancesResult&, const general::Address&) override;
  void TokenTransfersGet(api::TokenTransfersResult&, const general::Address& token, int64_t offset, int64_t limit) override;
  void TokenTransferGet(api::TokenTransfersResult& _return, const general::Address& token, const TransactionId& id) override;
  void TokenWalletTransfersGet(api::TokenTransfersResult&, const general::Address& token, const general::Address& address, int64_t offset, int64_t limit) override;
  void TokenTransactionsGet(api::TokenTransactionsResult&, const general::Address&, int64_t offset, int64_t limit) override;
  void TokenInfoGet(api::TokenInfoResult&, const general::Address&) override;
  void TokenHoldersGet(api::TokenHoldersResult&,const general::Address&, int64_t offset, int64_t limit, const TokenHoldersSortField order, const bool desc) override;
  void TokensListGet(api::TokensListResult&, int64_t offset, int64_t limit, const TokensListSortField order, const bool desc) override;
#ifdef TRANSACTIONS_INDEX
  void TokenTransfersListGet(api::TokenTransfersResult&, int64_t offset, int64_t limit) override;
  void TransactionsListGet(api::TransactionsGetResult&, int64_t offset, int64_t limit) override;
#endif
  void WalletsGet(api::WalletsGetResult& _return, int64_t offset, int64_t limit, int8_t ordCol, bool desc) override;
  void TrustedGet(api::TrustedGetResult& _return, int32_t page) override;
  ////////new

  void SyncStateGet(api::SyncStateResult& _return) override;

  BlockChain &get_s_blockchain() const noexcept { return s_blockchain; }

  executor::Executor &getExecutor() { return executor_; }

private:
  executor::Executor& executor_;

  struct smart_trxns_queue {
    cs::SpinLock lock;
    std::condition_variable_any new_trxn_cv{};
    size_t awaiter_num{0};
    std::deque<csdb::TransactionID> trid_queue{};
  };

  struct PendingSmartTransactions {
    std::queue<std::pair<cs::Sequence, csdb::Transaction>> queue;
    csdb::PoolHash last_pull_hash{};
    cs::Sequence last_pull_sequence = 0;
  };

  struct SmartState {
    std::string state;
    bool lastEmpty;
    csdb::TransactionID transaction;
    csdb::TransactionID initer;
  };

  using smart_state_entry = cs::WorkerQueue<SmartState>;
  using client_type = executor::ContractExecutorConcurrentClient;

  BlockChain& s_blockchain;
  cs::SolverCore& solver;
#ifdef MONITOR_NODE
  csstats::csstats stats;
#endif
  ::apache::thrift::stdcxx::shared_ptr<::apache::thrift::transport::TTransport> executorTransport_;

  struct SmartOperation {
    enum class State: uint8_t {
      Pending,
      Success,
      Failed
    };

    State state = State::Pending;
    csdb::TransactionID stateTransaction;

    bool hasRetval:1;
    bool returnsBool:1;
    bool boolResult:1;

    SmartOperation(): hasRetval(false), returnsBool(false) { }
    SmartOperation(const SmartOperation& rhs):
      state(rhs.state),
      stateTransaction(rhs.stateTransaction.clone()),
      hasRetval(rhs.hasRetval),
      returnsBool(rhs.returnsBool),
      boolResult(rhs.boolResult) { }

    //SmartOperation(SmartOperation&&) = delete; //not compiled!? (will not be called because there is "SmartOperation (const SmartOperation & rhs)")
    SmartOperation& operator=(const SmartOperation&) = delete;
    SmartOperation& operator=(SmartOperation&&) = delete;

    bool hasReturnValue() const { return hasRetval; }
    bool getReturnedBool() const { return returnsBool && boolResult; }
  };

  SmartOperation getSmartStatus(const csdb::TransactionID);

  cs::SpinLockable<std::map<csdb::TransactionID, SmartOperation>> smart_operations;
  cs::SpinLockable<std::map<cs::Sequence, std::vector<csdb::TransactionID>>> smarts_pending;

  cs::SpinLockable<std::map<csdb::Address, csdb::TransactionID>> smart_origin;
  cs::SpinLockable<std::map<csdb::Address, smart_state_entry>> smart_state;
  cs::SpinLockable<std::map<csdb::Address, smart_trxns_queue>> smart_last_trxn;
  cs::SpinLockable<std::map<csdb::Address, std::vector<csdb::TransactionID>>> deployed_by_creator;
  cs::SpinLockable<PendingSmartTransactions> pending_smart_transactions;
  std::map<csdb::PoolHash, api::Pool> poolCache;
  std::atomic_flag state_updater_running = ATOMIC_FLAG_INIT;
  std::thread state_updater;

  api::SmartContract fetch_smart_body(const csdb::Transaction&);

private:
  void state_updater_work_function();

  std::vector<api::SealedTransaction> extractTransactions(const csdb::Pool& pool, int64_t limit, const int64_t offset);

  api::SealedTransaction convertTransaction(const csdb::Transaction& transaction);

  std::vector<api::SealedTransaction> convertTransactions(const std::vector<csdb::Transaction>& transactions);

  api::Pool convertPool(const csdb::Pool& pool);

  api::Pool convertPool(const csdb::PoolHash& poolHash);

  //bool convertAddrToPublicKey(const csdb::Address& address);

  template <typename Mapper>
  size_t get_mapped_deployer_smart(const csdb::Address& deployer, Mapper mapper,
                                 std::vector<decltype(mapper(api::SmartContract()))>& out);

  bool update_smart_caches_once(const csdb::PoolHash&, bool = false);

  ::csdb::Transaction make_transaction(const ::api::Transaction&);
  void dumb_transaction_flow(api::TransactionFlowResult& _return, const ::api::Transaction&);
  void smart_transaction_flow(api::TransactionFlowResult& _return, const ::api::Transaction&);

  TokensMaster tm;

  const uint32_t MAX_EXECUTION_TIME = 1000;
};
}  // namespace api

bool is_deploy_transaction(const csdb::Transaction& tr);
bool is_smart(const csdb::Transaction& tr);
bool is_smart_state(const csdb::Transaction& tr);
bool is_smart_deploy(const api::SmartContractInvocation& smart);

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif  // APIHANDLER_HPP
