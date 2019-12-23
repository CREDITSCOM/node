#ifndef APIHANDLER_HPP
#define APIHANDLER_HPP

#include <csstats.hpp>

#include <deque>
#include <queue>
#include <tuple>

#include <client/params.hpp>

#include <lib/system/concurrent.hpp>

#include "tokens.hpp"
#include "dumbcv.hpp"
#include "executor.hpp"

namespace csconnector {
class connector;
}  // namespace csconnector

class APIHandlerBase {
public:
    enum class APIRequestStatusType : uint8_t {
        SUCCESS,
        FAILURE,
        NOT_IMPLEMENTED,
        NOT_FOUND,
        INPROGRESS,
        MAX
    };

    static void SetResponseStatus(general::APIResponse& response, APIRequestStatusType status, const std::string& details = "");
    static void SetResponseStatus(general::APIResponse& response, bool commandWasHandled);
};

struct APIHandlerInterface : public api::APINull, public APIHandlerBase {};

namespace cs {
class SolverCore;
class SmartContracts;
}

namespace apiexec {
class APIEXECHandler : public APIEXECNull, public APIHandlerBase {
public:
    explicit APIEXECHandler(BlockChain& blockchain, cs::SolverCore& _solver, cs::Executor& executor);
    APIEXECHandler(const APIEXECHandler&) = delete;
    void GetSeed(apiexec::GetSeedResult& _return, const general::AccessID accessId) override;
    void SendTransaction(apiexec::SendTransactionResult& _return, const general::AccessID accessId, const api::Transaction& transaction) override;
    void WalletIdGet(api::WalletIdGetResult& _return, const general::AccessID accessId, const general::Address& address) override;
    void SmartContractGet(SmartContractGetResult& _return, const general::AccessID accessId, const general::Address& address) override;
    void WalletBalanceGet(api::WalletBalanceGetResult& _return, const general::Address& address) override;
    void PoolGet(PoolGetResult& _return, const int64_t sequence) override;
    void GetDateTime(GetDateTimeResult& _return, const general::AccessID accessId) override;

    cs::Executor& getExecutor() const {
        return executor_;
    }

private:
    cs::Executor& executor_;
    BlockChain& blockchain_;
    cs::SolverCore& solver_;
};
}  // namespace apiexec

namespace api {
class APIHandler : public APIHandlerInterface {
public:
    explicit APIHandler(BlockChain& blockchain, cs::SolverCore& _solver, cs::Executor& executor);
    ~APIHandler() override;

    APIHandler(const APIHandler&) = delete;

    void WalletDataGet(api::WalletDataGetResult& _return, const general::Address& address) override;
    void WalletIdGet(api::WalletIdGetResult& _return, const general::Address& address) override;
    void WalletTransactionsCountGet(api::WalletTransactionsCountGetResult& _return, const general::Address& address) override;
    void WalletBalanceGet(api::WalletBalanceGetResult& _return, const general::Address& address) override;

    void TransactionGet(api::TransactionGetResult& _return, const api::TransactionId& transactionId) override;
    void TransactionsGet(api::TransactionsGetResult& _return, const general::Address& address, const int64_t offset, const int64_t limit) override;
    void TransactionFlow(api::TransactionFlowResult& _return, const api::Transaction& transaction) override;

    // Get list of pools from last one (head pool) to the first one.
    void PoolListGet(api::PoolListGetResult& _return, const int64_t offset, const int64_t limit) override;

    // Get pool info by pool hash. Starts looking from last one (head pool).
    void PoolInfoGet(api::PoolInfoGetResult& _return, const int64_t sequence, const int64_t index) override;
    void PoolTransactionsGet(api::PoolTransactionsGetResult& _return, const int64_t sequence, const int64_t offset, const int64_t limit) override;
    void StatsGet(api::StatsGetResult& _return) override;

    void SmartContractGet(api::SmartContractGetResult& _return, const general::Address& address) override;

    void SmartContractsListGet(api::SmartContractsListGetResult& _return, const general::Address& deployer, const int64_t offset, const int64_t limit) override;

    void SmartContractAddressesListGet(api::SmartContractAddressesListGetResult& _return, const general::Address& deployer) override;

    void GetLastHash(api::PoolHash& _return) override;
    void PoolListGetStable(api::PoolListGetResult& _return, const int64_t sequence, const int64_t limit) override;

    void WaitForSmartTransaction(api::TransactionId& _return, const general::Address& smart_public) override;

    void SmartContractsAllListGet(api::SmartContractsListGetResult& _return, const int64_t offset, const int64_t limit) override;

    void WaitForBlock(PoolHash& _return, const PoolHash& obsolete) override;

    void SmartMethodParamsGet(SmartMethodParamsGetResult& _return, const general::Address& address, const int64_t id) override;

    void TransactionsStateGet(TransactionsStateGetResult& _return, const general::Address& address, const std::vector<int64_t>& v) override;

    void ContractAllMethodsGet(ContractAllMethodsGetResult& _return, const std::vector<::general::ByteCodeObject>& byteCodeObjects) override;

    void addTokenResult(api::TokenTransfersResult& _return, const csdb::Address& token, const std::string& code, const csdb::Pool& pool, const csdb::Transaction& tr,
        const api::SmartContractInvocation& smart, const std::pair<csdb::Address, csdb::Address>& addrPair);

    void addTokenResult(api::TokenTransactionsResult& _return, const csdb::Address& token, const std::string&, const csdb::Pool& pool, const csdb::Transaction& tr,
        const api::SmartContractInvocation& smart, const std::pair<csdb::Address, csdb::Address>&);

    template <typename ResultType>
    void tokenTransactionsInternal(ResultType& _return, APIHandler& handler, TokensMaster& tm, const general::Address& token, bool transfersOnly, bool filterByWallet, int64_t offset,
        int64_t limit, const csdb::Address& wallet = csdb::Address());

    void ExecuteCountGet(ExecuteCountGetResult& _return, const std::string& executeMethod) override;

    void iterateOverTokenTransactions(const csdb::Address&, const std::function<bool(const csdb::Pool&, const csdb::Transaction&)>);

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
    void TokenHoldersGet(api::TokenHoldersResult&, const general::Address&, int64_t offset, int64_t limit, const TokenHoldersSortField order, const bool desc) override;
    void TokensListGet(api::TokensListResult&, int64_t offset, int64_t limit, const TokensListSortField order, const bool desc, const TokenFilters& filters) override;
    void TokenTransfersListGet(api::TokenTransfersResult&, int64_t offset, int64_t limit) override;
    void TransactionsListGet(api::TransactionsGetResult&, int64_t offset, int64_t limit) override;
    void WalletsGet(api::WalletsGetResult& _return, int64_t offset, int64_t limit, int8_t ordCol, bool desc) override;
    void TrustedGet(api::TrustedGetResult& _return, int32_t page) override;

    void SyncStateGet(api::SyncStateResult& _return) override;

    BlockChain& get_s_blockchain() const noexcept {
        return blockchain_;
    }

    cs::Executor& getExecutor() {
        return executor_;
    }

    bool isBDLoaded() { return isBDLoaded_; }
    
private:
#ifdef USE_DEPRECATED_STATS
    ::csstats::AllStats stats_;
#endif // 0
    cs::Executor& executor_;
    cs::DumbCv dumbCv_;

    bool isBDLoaded_{ false };

    struct smart_trxns_queue {
        cs::SpinLock lock{ATOMIC_FLAG_INIT};
        std::condition_variable_any new_trxn_cv{};
        size_t awaiter_num{0};
        std::deque<csdb::TransactionID> trid_queue{};
    };

    struct PendingSmartTransactions {
        std::queue<std::pair<cs::Sequence, csdb::Transaction>> queue;
        csdb::PoolHash last_pull_hash{};
        cs::Sequence last_pull_sequence = 0;
    };

    struct HashState {
        cs::Hash hash{};
        std::string retVal{};
        bool isOld{false};
        bool condFlg{false};
        csdb::TransactionID id{};
        cs::DumbCv::Condition condition{};
    };

    using client_type = executor::ContractExecutorConcurrentClient;
    using smartHashStateEntry = cs::WorkerQueue<HashState>;

    BlockChain& blockchain_;
    cs::SolverCore& solver_;
#ifdef USE_DEPRECATED_STATS // MONITOR_NODE
    csstats::csstats stats;
#endif

    struct SmartOperation {
        enum class State : uint8_t {
            Pending,
            Success,
            Failed
        };

        State state = State::Pending;
        csdb::TransactionID stateTransaction;

        bool hasRetval : 1;
        bool returnsBool : 1;
        bool boolResult : 1;

        SmartOperation()
        : hasRetval(false)
        , returnsBool(false) {
        }
        SmartOperation(const SmartOperation& rhs)
        : state(rhs.state)
        , stateTransaction(rhs.stateTransaction.clone())
        , hasRetval(rhs.hasRetval)
        , returnsBool(rhs.returnsBool)
        , boolResult(rhs.boolResult) {
        }

        // SmartOperation(SmartOperation&&) = delete; //not compiled!? (will not be called because there is "SmartOperation (const SmartOperation & rhs)")
        SmartOperation& operator=(const SmartOperation&) = delete;
        SmartOperation& operator=(SmartOperation&&) = delete;

        bool hasReturnValue() const {
            return hasRetval;
        }
        bool getReturnedBool() const {
            return returnsBool && boolResult;
        }
    };

    SmartOperation getSmartStatus(const csdb::TransactionID);

    cs::SpinLockable<std::map<csdb::TransactionID, SmartOperation>> smart_operations;
    cs::SpinLockable<std::map<cs::Sequence, std::vector<csdb::TransactionID>>> smarts_pending;

    cs::SpinLockable<std::map<csdb::Address, csdb::TransactionID>> smart_origin;
    cs::SpinLockable<std::map<csdb::Address, smart_trxns_queue>> smartLastTrxn_;

    cs::SpinLockable<std::map<cs::Signature, std::shared_ptr<smartHashStateEntry>>> hashStateSL;

    cs::SpinLockable<std::map<csdb::Address, std::vector<csdb::TransactionID>>> deployedByCreator_;

    cs::SpinLockable < std::map<cs::Sequence, api::Pool> > poolCache;
    std::atomic_flag state_updater_running = ATOMIC_FLAG_INIT;
    std::thread state_updater;

    std::map<std::string, int64_t> mExecuteCount_;

    api::SmartContract fetch_smart_body(const csdb::Transaction&);

private:
    std::vector<api::SealedTransaction> extractTransactions(const csdb::Pool& pool, int64_t limit, const int64_t offset);

    api::SealedTransaction convertTransaction(const csdb::Transaction& transaction);

    std::vector<api::SealedTransaction> convertTransactions(const std::vector<csdb::Transaction>& transactions);

    api::Pool convertPool(const csdb::Pool& pool);

    api::Pool convertPool(const csdb::PoolHash& poolHash);

    template <typename Mapper>
    size_t getMappedDeployerSmart(const csdb::Address& deployer, Mapper mapper, std::vector<decltype(mapper(api::SmartContract()))>& out, int64_t offset = 0, int64_t limit = 0);

    bool updateSmartCachesTransaction(csdb::Transaction trxn, cs::Sequence sequence);

    void run();

    ::csdb::Transaction makeTransaction(const ::api::Transaction&);
    void dumbTransactionFlow(api::TransactionFlowResult& _return, const csdb::Transaction& tr);
    void smartTransactionFlow(api::TransactionFlowResult& _return, const ::api::Transaction&, csdb::Transaction& send_transaction);

    std::optional<std::string> checkTransaction(const ::api::Transaction&, csdb::Transaction& cTransaction);

    TokensMaster tm_;

    const uint8_t ERROR_CODE = 1;

    friend class ::csconnector::connector;

    std::condition_variable_any newBlockCv_;
    std::mutex dbLock_;

    cs::Sequence maxReadSequence{};

private slots:
    void updateSmartCachesPool(const csdb::Pool& pool);
    void store_block_slot(const csdb::Pool& pool);
    void collect_all_stats_slot(const csdb::Pool& pool);
    void baseLoaded(const csdb::Pool& pool);
    void maxBlocksCount(cs::Sequence lastBlockNum);
    void onPacketExpired(const cs::TransactionsPacket& packet);
};
}  // namespace api

bool is_deploy_transaction(const csdb::Transaction& tr);
bool is_smart(const csdb::Transaction& tr);
bool is_smart_state(const csdb::Transaction& tr);
bool is_smart_deploy(const api::SmartContractInvocation& smart);

#endif  // APIHANDLER_HPP
