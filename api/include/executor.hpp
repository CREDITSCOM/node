#ifndef EXECUTOR_HPP
#define EXECUTOR_HPP

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include <API.h>
#include <APIEXEC.h>
#include <ContractExecutor.h>
#include <executor_types.h>
#include <general_types.h>

#include <thrift/transport/TSocket.h>
#include <thrift/protocol/TBinaryProtocol.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <any>
#include <memory>
#include <optional>

#include <lib/system/common.hpp>
#include <lib/system/process.hpp>
#include <lib/system/reference.hpp>

#include <csnode/blockchain.hpp>

#include <csdb/currency.hpp>

#include "executormanager.hpp"

class BlockChain;

namespace cs {
class Executor;
class SolverCore;

struct ExecutorSettings {
    using Types = std::tuple<cs::Reference<const BlockChain>, cs::Reference<const cs::SolverCore>>;

    static void set(cs::Reference<const BlockChain> blockchain, cs::Reference<const cs::SolverCore> solver);

private:
    static Types get();

    inline static std::any blockchain_;
    inline static std::any solver_;

    friend class Executor;
};

class Executor {
public:
    // Convention how to pass the method name
    enum class MethodNameConvention {
        // By default, the method name can be obtained from SmartContractInvocation object deserialized from user_field[0]
        // If method name is empty, the constructor must be called
        Default = 0,
        // Call to payable(string, string) requested
        PayableLegacy,
        // Call to payable(string, byte[]) requested
        Payable
    };

    enum ExecutorErrorCode {
        NoError = 0,
        GeneralError = 1,
        IncorrecJdkVersion,
        ServerStartError
    };

    enum class ExecutorState {
        Idle,
        Launching,
        Launched
    };

    enum ACCESS_ID_RESERVE {
        GETTER,
        START_INDEX
    };

    struct ExecuteTransactionInfo {
        // transaction to execute contract
        csdb::Transaction transaction;
        // transaction containing deploy info
        csdb::Transaction deploy;
        // pass method name convention
        MethodNameConvention convention;
        // max allowed fee
        csdb::Amount feeLimit;
        // block sequnece
        cs::Sequence sequence;
    };

    struct ExecuteResult {
        struct EmittedTrxn {
            csdb::Address source;
            csdb::Address target;
            csdb::Amount amount;
            std::string userData;
        };

        struct SmartRes {
            general::Variant retValue;
            std::map<csdb::Address, std::string> states;
            std::vector<EmittedTrxn> emittedTransactions;
            int64_t executionCost; // measured in milliseconds actual cost of execution
            ::general::APIResponse response;
        };

        ::general::APIResponse response;
        std::vector<SmartRes> smartsRes;
        long selfMeasuredCost; // measured in milliseconds total cost of executions
    };

    struct OriginExecuteResult {
        executor::ExecuteByteCodeResult resp;
        general::AccessID acceessId;
        // measured execution duration in milliseconds
        long long timeExecute;
    };

    // Pass kUseLastSequence to executeByteCode...() to use current last sequence automatically
    static constexpr cs::Sequence kUseLastSequence = 0;

    void executeByteCode(executor::ExecuteByteCodeResult& resp, const std::string& address, const std::string& smart_address, const std::vector<general::ByteCodeObject>& code,
        const std::string& state, std::vector<executor::MethodHeader>& methodHeader, bool isGetter, cs::Sequence sequence);

    void executeByteCodeMultiple(executor::ExecuteByteCodeMultipleResult& _return, const ::general::Address& initiatorAddress, const executor::SmartContractBinary& invokedContract,
        const std::string& method, const std::vector<std::vector<::general::Variant>>& params, const int64_t executionTime, cs::Sequence sequence);

    void getContractMethods(executor::GetContractMethodsResult& _return, const std::vector<::general::ByteCodeObject>& byteCodeObjects);
    void getContractVariables(executor::GetContractVariablesResult& _return, const std::vector<::general::ByteCodeObject>& byteCodeObjects, const std::string& contractState);

    void compileSourceCode(executor::CompileSourceCodeResult& _return, const std::string& sourceCode);
    void getExecutorBuildVersion(executor::ExecutorBuildVersionResult& _return);

    static Executor& instance();

    bool isConnected() const;
    void stop();

    std::optional<cs::Sequence> getSequence(const general::AccessID& accessId);
    std::optional<csdb::TransactionID> getDeployTrxn(const csdb::Address& address);

    void updateDeployTrxns(const csdb::Address& address, const csdb::TransactionID& trxnsId);
    void setLastState(const csdb::Address& address, const std::string& state);

    std::optional<std::string> getState(const csdb::Address& address);

    void updateCacheLastStates(const csdb::Address& address, const cs::Sequence& sequence, const std::string& state);
    std::optional<std::string> getAccessState(const general::AccessID& accessId, const csdb::Address& address);

    void addInnerSendTransaction(const general::AccessID& accessId, const csdb::Transaction& transaction);
    std::optional<std::vector<csdb::Transaction>> getInnerSendTransactions(const general::AccessID& accessId);
    void deleteInnerSendTransactions(const general::AccessID& accessId);

    bool isDeploy(const csdb::Transaction& transaction);

    /**
     * Executes the transaction operation
     *
     * @param   smarts              The list of smart contract related transactions to execute.
     * @param   forceContractState  The forced state of the contract to use in execution, if not empty overrides stored state in blocks.
     * @param   validationMode      True to enable validation mode, false to disable it. If set to true the execution is only for validation,
     *                              so any contract can (and must) be modified. The result is guaranteed not to put to chain
     *
     * @returns A std::optional&lt;ExecuteResult&gt;
     */

    std::optional<ExecuteResult> executeTransaction(const std::vector<ExecuteTransactionInfo>& smarts, std::string forceContractState);
    std::optional<ExecuteResult> reexecuteContract(ExecuteTransactionInfo& contract, std::string forceContractState);

    csdb::Transaction makeTransaction(const api::Transaction& transaction);

    void stateUpdate(const csdb::Pool& pool);

    void addToLockSmart(const general::Address& address, const general::AccessID& accessId);
    void deleteFromLockSmart(const general::Address& address, const general::AccessID& accessId);

    bool isLockSmart(const general::Address& address, const general::AccessID& accessId);

    mutable std::mutex blockMutex_;

    // equivalent access to the blockchain for api and other threads
    template<typename T, typename = std::enable_if_t<std::is_same_v<T, csdb::PoolHash> || std::is_same_v<T, cs::Sequence>>>
    csdb::Pool loadBlockApi(const T& p) const {
        std::lock_guard lock(blockMutex_);
        return blockchain_.loadBlock(p);
    }

    csdb::Transaction loadTransactionApi(const csdb::TransactionID& id) const;

    uint64_t getTimeSmartContract(general::AccessID accessId);

public slots:
    void onBlockStored(const csdb::Pool& pool);
    void onReadBlock(const csdb::Pool& block);

    void onExecutorStarted();
    void onExecutorFinished(int code, const std::error_code&);
    void onExecutorProcessError(const cs::ProcessException& exception);

    void checkExecutorVersion();

private:
    int commitMin_{};
    int commitMax_{};

    std::map<general::Address, general::AccessID> lockSmarts_;

    explicit Executor(const ExecutorSettings::Types& types);
    ~Executor();

    void runProcess();
    void checkAnotherExecutor();

    // The explicit_sequence is set for generated accessId ensure having correct sequence attached to it
    uint64_t generateAccessId(cs::Sequence explicitSequence, const csdb::Address& smartAddress);
    uint64_t getFutureAccessId();
    void deleteAccessId(const general::AccessID& accessId);

    // explicit sequence sets the sequence for accessId attached to execution
    std::optional<OriginExecuteResult> execute(const std::string& address, const executor::SmartContractBinary& smartContractBinary,
        std::vector<executor::MethodHeader>& methodHeader, bool isGetter, cs::Sequence explicitSequence);

    bool connect();
    void disconnect();
    void notifyError();

    using OriginExecutor = executor::ContractExecutorConcurrentClient;
    using BinaryProtocol = apache::thrift::protocol::TBinaryProtocol;

    void recreateOriginExecutor();

private:
    const BlockChain& blockchain_;
    const cs::SolverCore& solver_;

    ::apache::thrift::stdcxx::shared_ptr<::apache::thrift::transport::TSocket> socket_;
    ::apache::thrift::stdcxx::shared_ptr<::apache::thrift::transport::TTransport> executorTransport_;

    std::unique_ptr<executor::ContractExecutorConcurrentClient> origExecutor_;
    std::unique_ptr<cs::Process> executorProcess_;

    general::AccessID lastAccessId_{};
    std::map<general::AccessID, cs::Sequence> accessSequence_;
    std::map<general::AccessID, csdb::Address> executableSmartAddress_;
    std::map<csdb::Address, csdb::TransactionID> deployTrxns_;
    std::map<csdb::Address, std::string> lastState_;
    std::map<csdb::Address, std::unordered_map<cs::Sequence, std::string>> cacheLastStates_;
    std::map<general::AccessID, std::vector<csdb::Transaction>> innerSendTransactions_;

    std::shared_mutex sharedErrorMutex_;
    std::shared_mutex mutex_;
    std::atomic_size_t execCount_{0};

    mutable std::condition_variable cvErrorConnect_;
    std::atomic_bool requestStop_{ false };

    const int16_t EXECUTOR_VERSION = 3;

    std::mutex callExecutorLock_;
    std::atomic<bool> isWatcherRunning_ = { false };

    cs::ExecutorManager manager_;
    ExecutorState state_;

    std::map<int, const char*> executorMessages_ = {
        { NoError, "Executor finished with no error code" },
        { GeneralError, "Executor unexpected error, try to launch again" },
        { IncorrecJdkVersion, "Executor can not be launched due to incorred JDK version" },
        { ServerStartError, "Executor server start error" }
    };
};
}

#endif // EXECUTOR_HPP
