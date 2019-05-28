#ifndef APIHANDLER_HPP
#define APIHANDLER_HPP

#if defined(_MSC_VER)
#pragma warning(push)
// 4706 - assignment within conditional expression
// 4373 - 'api::APIHandler::TokenTransfersListGet': virtual function overrides 'api::APINull::TokenTransfersListGet',
//         previous versions of the compiler did not override when parameters only differed by const/volatile qualifiers
// 4245 - 'return' : conversion from 'int' to 'SOCKET', signed / unsigned mismatch
#pragma warning(disable : 4706 4373 4245) 
#endif

#include <API.h>
#include <APIEXEC.h>
#include <executor_types.h>
#include <general_types.h>

#include <thrift/transport/TSocket.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <csnode/blockchain.hpp>

#include <csstats.hpp>
#include <deque>
#include <queue>

#include <client/params.hpp>
#include <lib/system/concurrent.hpp>

#include "tokens.hpp"

#include <optional>

#include <csdb/currency.hpp>
#include <solvercore.hpp>

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
        MAX,
		INPROGRESS
    };

    static void SetResponseStatus(general::APIResponse& response, APIRequestStatusType status, const std::string& details = "");
    static void SetResponseStatus(general::APIResponse& response, bool commandWasHandled);
};

struct APIHandlerInterface : public api::APINull, public APIHandlerBase {};

template <typename T>
T deserialize(std::string&& s) {
    using namespace ::apache;

    // https://stackoverflow.com/a/16261758/2016154
    static_assert(CHAR_BIT == 8 && std::is_same<std::uint8_t, unsigned char>::value, "This code requires std::uint8_t to be implemented as unsigned char.");

    const auto buffer = thrift::stdcxx::make_shared<thrift::transport::TMemoryBuffer>(reinterpret_cast<uint8_t*>(&(s[0])), static_cast<uint32_t>(s.size()));
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
}  // namespace executor

namespace executor {
class Executor {
public:  // wrappers
    void executeByteCode(executor::ExecuteByteCodeResult& resp, const std::string& address, const std::string& smart_address, const std::vector<general::ByteCodeObject>& code,
                         const std::string& state, const std::string& method, const std::vector<general::Variant>& params, const int64_t& timeout) {
        csunused(timeout);
        static std::mutex m;
        std::lock_guard lk(m);  // temporary solution

        if (!code.empty()) {
            executor::SmartContractBinary smartContractBinary;
            smartContractBinary.contractAddress = smart_address;
            smartContractBinary.object.byteCodeObjects = code;
            smartContractBinary.object.instance = state;
            smartContractBinary.stateCanModify = solver_.isContractLocked(BlockChain::getAddressFromKey(smart_address)) ? true : false;
            if (auto optOriginRes = execute(address, smartContractBinary, method, params))
                resp = optOriginRes.value().resp;
        }
    }

    void executeByteCodeMultiple(ExecuteByteCodeMultipleResult& _return, const ::general::Address& initiatorAddress, const SmartContractBinary& invokedContract,
        const std::string& method, const std::vector<std::vector<::general::Variant>>& params, const int64_t executionTime) {
        if (!connect()) {
            _return.status.code = 1;
            _return.status.message = "No executor connection!";
            return;
        }
        const auto acceess_id = generateAccessId();
        ++execCount_;
        try {
			std::shared_lock slk(shErrMt);
            origExecutor_->executeByteCodeMultiple(_return, acceess_id, initiatorAddress, invokedContract, method, params, executionTime, EXECUTOR_VERSION);
        }
        catch (::apache::thrift::transport::TTransportException & x) {
            // sets stop_ flag to true forever, replace with new instance
            if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
                reCreationOriginExecutor();
            }
            _return.status.code = 1;
            _return.status.message = x.what();
        }
        catch( std::exception & x ) {
            _return.status.code = 1;
            _return.status.message = x.what();
        }
        --execCount_;
        deleteAccessId(acceess_id);
        disconnect();
    }

    void getContractMethods(GetContractMethodsResult& _return, const std::vector<::general::ByteCodeObject>& byteCodeObjects) {
        if (!connect()) {
            _return.status.code = 1;
            _return.status.message = "No executor connection!";
            return;
        }
        try {
			std::shared_lock slk(shErrMt);
            origExecutor_->getContractMethods(_return, byteCodeObjects, EXECUTOR_VERSION);
        }
        catch (::apache::thrift::transport::TTransportException & x) {
            // sets stop_ flag to true forever, replace with new instance
            if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
                reCreationOriginExecutor();
            }
            _return.status.code = 1;
            _return.status.message = x.what();
        }
        catch( std::exception & x ) {
            _return.status.code = 1;
            _return.status.message = x.what();
        }
        disconnect();
    }

    void getContractVariables(GetContractVariablesResult& _return, const std::vector<::general::ByteCodeObject>& byteCodeObjects, const std::string& contractState) {
        if (!connect()) {
            _return.status.code = 1;
            _return.status.message = "No executor connection!";
            return;
        }
        try {
			std::shared_lock slk(shErrMt);
            origExecutor_->getContractVariables(_return, byteCodeObjects, contractState, EXECUTOR_VERSION);
        }
        catch (::apache::thrift::transport::TTransportException & x) {
            // sets stop_ flag to true forever, replace with new instance
            if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
                reCreationOriginExecutor();
            }
            _return.status.code = 1;
            _return.status.message = x.what();
        }
        catch( std::exception & x ) {
            _return.status.code = 1;
            _return.status.message = x.what();
        }
        disconnect();
    }

    void compileSourceCode(CompileSourceCodeResult& _return, const std::string& sourceCode) {
        if (!connect()) {
            _return.status.code = 1;
            _return.status.message = "No executor connection!";
            return;
        }
        try {
			std::shared_lock slk(shErrMt);
            origExecutor_->compileSourceCode(_return, sourceCode, EXECUTOR_VERSION);
        }
        catch (::apache::thrift::transport::TTransportException & x) {
            // sets stop_ flag to true forever, replace with new instance
            if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
                reCreationOriginExecutor();
            }
            _return.status.code = 1;
            _return.status.message = x.what();
        }
        catch( std::exception & x ) {
            _return.status.code = 1;
            _return.status.message = x.what();
        }
        disconnect();
    }

public:
    static Executor& getInstance(const BlockChain* p_blockchain = nullptr, const cs::SolverCore* solver = nullptr, const int p_exec_port = 0) {  // singlton
        static Executor executor(*p_blockchain, *solver, p_exec_port);
        return executor;
    }

    std::optional<cs::Sequence> getSequence(const general::AccessID& accessId) {
        std::shared_lock slk(mtx_);
        if (auto it = accessSequence_.find(accessId); it != accessSequence_.end())
            return std::make_optional(it->second);
        return std::nullopt;
    }

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
            for (const auto& [curr_seq, curr_state] : unmap_states_it->second) {
                if (curr_seq > access_sequence)
                    return prev_seq_state.first ? std::make_optional<std::string>(prev_seq_state.second) : std::nullopt;
                prev_seq_state = {curr_seq, curr_state};
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

    void addInnerSendTransaction(const general::AccessID& accessId, const csdb::Transaction& transaction) {
        std::lock_guard lk(mtx_);
        innerSendTransactions_[accessId].push_back(transaction);
    }

    std::optional<std::vector<csdb::Transaction>> getInnerSendTransactions(const general::AccessID& accessId) {
        std::shared_lock slk(mtx_);
        if (const auto it = innerSendTransactions_.find(accessId); it != innerSendTransactions_.end())
            return std::make_optional<std::vector<csdb::Transaction>>(it->second);
        return std::nullopt;
    }

    void deleteInnerSendTransactions(const general::AccessID& accessId) {
        std::lock_guard lk(mtx_);
        innerSendTransactions_.erase(accessId);
    }

    bool isDeploy(const csdb::Transaction& trxn) {
        if (trxn.user_field(0).is_valid()) {
            const auto sci = deserialize<api::SmartContractInvocation>(trxn.user_field(0).value<std::string>());
            if (sci.method.empty())
                return true;
        }
        return false;
    }

    std::optional<ExecuteResult> executeTransaction(const csdb::Pool& pool, const uint64_t& offsetTrx, const csdb::Amount& feeLimit, const std::string& force_new_state) {
        csunused(feeLimit);
        static std::mutex m;
        std::lock_guard lk(m);  // temporary solution

        auto smartTrxn = *(pool.transactions().begin() + offsetTrx);

        auto smartSource = blockchain_.getAddressByType(smartTrxn.source(), BlockChain::AddressType::PublicKey);
        auto smartTarget = blockchain_.getAddressByType(smartTrxn.target(), BlockChain::AddressType::PublicKey);

        csdb::Transaction deployTrxn;
        const auto isdeploy = isDeploy(smartTrxn);
        if (!isdeploy) {  // execute
            const auto optDeployId = getDeployTrxn(smartTarget);
            if (!optDeployId.has_value())
                return std::nullopt;
            deployTrxn = blockchain_.loadTransaction(optDeployId.value());
        }
        else
            deployTrxn = smartTrxn;

        const auto sci_deploy = deserialize<api::SmartContractInvocation>(deployTrxn.user_field(0).value<std::string>());
        executor::SmartContractBinary smartContractBinary;
        smartContractBinary.contractAddress = smartTarget.to_api_addr();
        smartContractBinary.object.byteCodeObjects = sci_deploy.smartContractDeploy.byteCodeObjects;
        // may contain temporary last new state not yet written into block chain (to allow "speculative" multi-executions af the same contract)
        if (!force_new_state.empty()) {
            smartContractBinary.object.instance = force_new_state;
        }
        else {
            auto optState = getState(smartTarget);
            if (optState.has_value())
                smartContractBinary.object.instance = optState.value();
        }
        smartContractBinary.stateCanModify = solver_.isContractLocked(BlockChain::getAddressFromKey(smartTarget.to_api_addr())) ? true : false;

        std::string method;
        std::vector<general::Variant> params;
        api::SmartContractInvocation sci;
        if (!smartTrxn.user_field(0).is_valid() && smartTrxn.amount().to_double()) {  // payable
            method = "payable";
            general::Variant var;
            var.__set_v_string(smartTrxn.amount().to_string());
            params.emplace_back(var);

			if (smartTrxn.user_field(1).is_valid()) {
				var.__set_v_string(smartTrxn.user_field(1).value<std::string>());
				params.emplace_back(var);
			}
			else {
				var.__set_v_string("");
				params.emplace_back(var);
			}
        }
        else if (!isdeploy) {
            sci = deserialize<api::SmartContractInvocation>(smartTrxn.user_field(0).value<std::string>());
            method = sci.method;
            params = sci.params;

            for (const auto& addrLock : sci.usedContracts) {
                addToLockSmart(addrLock, getFutureAccessId());
            }
        }

        const auto optOriginRes = execute(smartSource.to_api_addr(), smartContractBinary, method, params);

        if (!isdeploy) {
            for (const auto& addrLock : sci.usedContracts) {
                deleteFromLockSmart(addrLock, getFutureAccessId());
            }
        }

        if (!optOriginRes.has_value())
            return std::nullopt;

        const auto optInnerTransactions = getInnerSendTransactions(optOriginRes.value().acceessId);

        ExecuteResult res;
        const auto resp = optOriginRes.value().resp;
        if (resp.status.code != 0) {
            if (!resp.status.message.empty()) {
                res.retValue.__set_v_string(resp.status.message);
            }
            else {
                res.retValue = resp.ret_val;
            }
        }
        else {
            if (optInnerTransactions.has_value())
                res.trxns = optInnerTransactions.value();
            deleteInnerSendTransactions(optOriginRes.value().acceessId);
            constexpr double FEE_IN_SECOND = kMinFee * 4.0;
            const double fee = std::min(kMinFee, static_cast<double>(optOriginRes.value().timeExecute) * FEE_IN_SECOND);
            res.fee = csdb::Amount(fee);
            res.newState = optOriginRes.value().resp.invokedContractState;
            for (const auto& [itAddress, itState] : optOriginRes.value().resp.externalContractsState) {
                const csdb::Address addr = BlockChain::getAddressFromKey(itAddress);
                res.states[addr] = itState;
            }
            res.retValue = optOriginRes.value().resp.ret_val;
        }
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

    bool isConnect() {
        return isConnect_;
    }

    void state_update(const csdb::Pool& pool) {
        if (!pool.transactions().size())
            return;
        for (const auto& trxn : pool.transactions()) {
            if (trxn.is_valid() && (trxn.user_field(-2).type() == csdb::UserField::Type::String && trxn.user_field(1).type() == csdb::UserField::Type::String)) {
                const auto address = blockchain_.getAddressByType(trxn.target(), BlockChain::AddressType::PublicKey);
                const auto newstate = trxn.user_field(-2).value<std::string>();
                if (!newstate.empty()) {
                    setLastState(address, newstate);
                    updateCacheLastStates(address, pool.sequence(), newstate);
                }
            }
        }
    }

    void addToLockSmart(const general::Address& address, const general::AccessID& accessId) {
        std::lock_guard lk(mtx_);
        lockSmarts[address] = accessId;
    }

    void deleteFromLockSmart(const general::Address& address, const general::AccessID& accessId) {
        csunused(accessId);
        std::lock_guard lk(mtx_);
        lockSmarts.erase(address);
    }

    bool isLockSmart(const general::Address& address, const general::AccessID& accessId) {
        std::lock_guard lk(mtx_);
        if (auto addrLock = lockSmarts.find(address); addrLock != lockSmarts.end() && addrLock->second == accessId)
            return true;
        return false;
    }

public slots:
    void onBlockStored(const csdb::Pool& pool) {
        state_update(pool);
    }

    void onReadBlock(const csdb::Pool& block, bool* test_failed) {
        csunused(test_failed);
        state_update(block);
    }

private:
    std::map<general::Address, general::AccessID> lockSmarts;
    explicit Executor(const BlockChain& p_blockchain, const cs::SolverCore& solver, const int p_exec_port)
    : blockchain_(p_blockchain)
    , solver_(solver)
    , executorTransport_(new ::apache::thrift::transport::TBufferedTransport(::apache::thrift::stdcxx::make_shared<::apache::thrift::transport::TSocket>("localhost", p_exec_port)))
    , origExecutor_(
          std::make_unique<executor::ContractExecutorConcurrentClient>(::apache::thrift::stdcxx::make_shared<apache::thrift::protocol::TBinaryProtocol>(executorTransport_))) {
        std::thread th([&]() {
            while (true) {
                if (isConnect_) {
                    static std::mutex mt;
                    std::unique_lock ulk(mt);
                    cvErrorConnect_.wait(ulk, [&] { return !isConnect_; });
                }

                static const int RECONNECT_TIME = 10;
                std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_TIME));
                if (connect())
                    disconnect();
            }
        });
        th.detach();
    }

    struct OriginExecuteResult {
        ExecuteByteCodeResult resp;
        general::AccessID acceessId;
        long long timeExecute;
    };

    uint64_t generateAccessId() {
        std::lock_guard lk(mtx_);
        ++lastAccessId_;
        accessSequence_[lastAccessId_] = blockchain_.getLastSequence();
        return lastAccessId_;
    }

    uint64_t getFutureAccessId() {
        return lastAccessId_ + 1;
    }

    void deleteAccessId(const general::AccessID& p_access_id) {
        std::lock_guard lk(mtx_);
        accessSequence_.erase(p_access_id);
    }

    std::optional<OriginExecuteResult> execute(const std::string& address, const SmartContractBinary& smartContractBinary, const std::string& method,
        const std::vector<general::Variant>& params) {
        constexpr uint64_t EXECUTION_TIME = Consensus::T_smart_contract;
        OriginExecuteResult originExecuteRes{};
        if (!connect())
            return std::nullopt;
        const auto access_id = generateAccessId();
        ++execCount_;
        const auto timeBeg = std::chrono::steady_clock::now();
        try {
			std::shared_lock slk(shErrMt);
            origExecutor_->executeByteCode(originExecuteRes.resp, access_id, address, smartContractBinary, method, params, EXECUTION_TIME, EXECUTOR_VERSION);
        }
        catch (::apache::thrift::transport::TTransportException & x) {
            // sets stop_ flag to true forever, replace with new instance
            if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
                reCreationOriginExecutor();
            }
            originExecuteRes.resp.status.code = 1;
            originExecuteRes.resp.status.message = x.what();
        }
        catch( std::exception & x ) {
            originExecuteRes.resp.status.code = 1;
            originExecuteRes.resp.status.message = x.what();
        }
        originExecuteRes.timeExecute = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - timeBeg).count();
        --execCount_;
        deleteAccessId(access_id);
        disconnect();
        originExecuteRes.acceessId = access_id;
        return std::make_optional<OriginExecuteResult>(std::move(originExecuteRes));
    }

    bool connect() {
        try {
            if (executorTransport_->isOpen())
                executorTransport_->close();

            executorTransport_->open();
            isConnect_ = true;
        }
        catch (...) {
            isConnect_ = false;
            cvErrorConnect_.notify_one();
        }
        return isConnect_;
    }

    void disconnect() {
        executorTransport_->close();
    }

	//
	using OriginExecutor = executor::ContractExecutorConcurrentClient;
	using BinaryProtocol = apache::thrift::protocol::TBinaryProtocol;
	std::shared_mutex shErrMt;
	void reCreationOriginExecutor() {
		std::lock_guard glk(shErrMt);
		origExecutor_.reset(new OriginExecutor(::apache::thrift::stdcxx::make_shared<BinaryProtocol>(executorTransport_)));
	}
	//	

private:
    const BlockChain& blockchain_;
    const cs::SolverCore& solver_;
    ::apache::thrift::stdcxx::shared_ptr<::apache::thrift::transport::TTransport> executorTransport_;
    std::unique_ptr<executor::ContractExecutorConcurrentClient> origExecutor_;

    general::AccessID lastAccessId_{};
    std::map<general::AccessID, cs::Sequence> accessSequence_;
    std::map<csdb::Address, csdb::TransactionID> deployTrxns_;
    std::map<csdb::Address, std::string> lastState_;
    std::map<csdb::Address, std::unordered_map<cs::Sequence, std::string>> cacheLastStates_;
    std::map<general::AccessID, std::vector<csdb::Transaction>> innerSendTransactions_;

    std::shared_mutex mtx_;
    std::atomic_size_t execCount_{0};

    std::condition_variable cvErrorConnect_;
    std::atomic_bool isConnect_{false};

    const uint16_t EXECUTOR_VERSION = 0;
};
}  // namespace executor
namespace apiexec {
class APIEXECHandler : public APIEXECNull, public APIHandlerBase {
public:
    explicit APIEXECHandler(BlockChain& blockchain, cs::SolverCore& _solver, executor::Executor& executor, const csconnector::Config& config);
    APIEXECHandler(const APIEXECHandler&) = delete;
    void GetSeed(apiexec::GetSeedResult& _return, const general::AccessID accessId) override;
    void SendTransaction(apiexec::SendTransactionResult& _return, const general::AccessID accessId, const api::Transaction& transaction) override;
    void WalletIdGet(api::WalletIdGetResult& _return, const general::AccessID accessId, const general::Address& address) override;
    void SmartContractGet(SmartContractGetResult& _return, const general::AccessID accessId, const general::Address& address) override;
    void WalletBalanceGet(api::WalletBalanceGetResult& _return, const general::Address& address) override;

    executor::Executor& getExecutor() const {
        return executor_;
    }

private:
    executor::Executor& executor_;
    BlockChain& blockchain_;
    cs::SolverCore& solver_;
};
}  // namespace apiexec

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
    void TransactionsGet(api::TransactionsGetResult& _return, const general::Address& address, const int64_t offset, const int64_t limit) override;
    void TransactionFlow(api::TransactionFlowResult& _return, const api::Transaction& transaction) override;

    // Get list of pools from last one (head pool) to the first one.
    void PoolListGet(api::PoolListGetResult& _return, const int64_t offset, const int64_t limit) override;

    // Get pool info by pool hash. Starts looking from last one (head pool).
    void PoolInfoGet(api::PoolInfoGetResult& _return, const api::PoolHash& hash, const int64_t index) override;
    void PoolTransactionsGet(api::PoolTransactionsGetResult& _return, const api::PoolHash& hash, const int64_t offset, const int64_t limit) override;
    void StatsGet(api::StatsGetResult& _return) override;

    void SmartContractGet(api::SmartContractGetResult& _return, const general::Address& address) override;

    void SmartContractsListGet(api::SmartContractsListGetResult& _return, const general::Address& deployer) override;

    void SmartContractAddressesListGet(api::SmartContractAddressesListGetResult& _return, const general::Address& deployer) override;

    void GetLastHash(api::PoolHash& _return) override;
    void PoolListGetStable(api::PoolListGetResult& _return, const api::PoolHash& hash, const int64_t limit) override;

    void WaitForSmartTransaction(api::TransactionId& _return, const general::Address& smart_public) override;

    void SmartContractsAllListGet(api::SmartContractsListGetResult& _return, const int64_t offset, const int64_t limit) override;

    void WaitForBlock(PoolHash& _return, const PoolHash& obsolete) override;

    void SmartMethodParamsGet(SmartMethodParamsGetResult& _return, const general::Address& address, const int64_t id) override;

    void TransactionsStateGet(TransactionsStateGetResult& _return, const general::Address& address, const std::vector<int64_t>& v) override;

    void ContractAllMethodsGet(ContractAllMethodsGetResult& _return, const std::vector<::general::ByteCodeObject>& byteCodeObjects) override;

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
    void TokenHoldersGet(api::TokenHoldersResult&, const general::Address&, int64_t offset, int64_t limit, const TokenHoldersSortField order, const bool desc) override;
    void TokensListGet(api::TokensListResult&, int64_t offset, int64_t limit, const TokensListSortField order, const bool desc) override;
#ifdef TRANSACTIONS_INDEX
    void TokenTransfersListGet(api::TokenTransfersResult&, int64_t offset, int64_t limit) override;
    void TransactionsListGet(api::TransactionsGetResult&, int64_t offset, int64_t limit) override;
#endif
    void WalletsGet(api::WalletsGetResult& _return, int64_t offset, int64_t limit, int8_t ordCol, bool desc) override;
    void TrustedGet(api::TrustedGetResult& _return, int32_t page) override;
    ////////new

    void SyncStateGet(api::SyncStateResult& _return) override;

    BlockChain& get_s_blockchain() const noexcept {
        return s_blockchain;
    }

    executor::Executor& getExecutor() {
        return executor_;
    }

private:
	::csstats::AllStats stats_;
    executor::Executor& executor_;

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
    cs::SpinLockable<std::map<csdb::Address, smart_state_entry>> smart_state;
    cs::SpinLockable<std::map<csdb::Address, smart_trxns_queue>> smart_last_trxn;

	//
	using TrxInPrgss = std::pair<csdb::Address, int64_t>;
	using CVInPrgss = std::pair<std::condition_variable, bool>;
	cs::SpinLockable<std::map<TrxInPrgss, CVInPrgss>> trxInprogress;
	//

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

    // bool convertAddrToPublicKey(const csdb::Address& address);

    template <typename Mapper>
    size_t getMappedDeployerSmart(const csdb::Address& deployer, Mapper mapper, std::vector<decltype(mapper(api::SmartContract()))>& out);

    bool update_smart_caches_once(const csdb::PoolHash&, bool = false);
    void run();

    ::csdb::Transaction make_transaction(const ::api::Transaction&);
    void dumb_transaction_flow(api::TransactionFlowResult& _return, const ::api::Transaction&);
    void smart_transaction_flow(api::TransactionFlowResult& _return, const ::api::Transaction&);

    TokensMaster tm;

    const uint32_t MAX_EXECUTION_TIME = 1000;

    const uint8_t ERROR_CODE = 1;

    friend class ::csconnector::connector;

    std::condition_variable_any newBlockCv_;
    std::mutex dbLock_;

private slots:
    void update_smart_caches_slot(const csdb::Pool& pool);
    void store_block_slot(const csdb::Pool& pool);
	void collect_all_stats_slot(const csdb::Pool& pool);
};
}  // namespace api

bool is_deploy_transaction(const csdb::Transaction& tr);
bool is_smart(const csdb::Transaction& tr);
bool is_smart_state(const csdb::Transaction& tr);
bool is_smart_deploy(const api::SmartContractInvocation& smart);

#endif  // APIHANDLER_HPP
