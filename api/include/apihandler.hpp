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
#include <lib/system/process.hpp>

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
        INPROGRESS,
        MAX
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
class SmartContracts;
std::string get_contract_state(const csdb::Address& abs_addr, const BlockChain& blockchain); // declared in SmartContracts
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
            const std::string& state, std::vector<MethodHeader>& methodHeader, bool isGetter) {
        static std::mutex mutex;
        std::lock_guard lock(mutex);  // temporary solution

        if (!code.empty()) {
            executor::SmartContractBinary smartContractBinary;
            smartContractBinary.contractAddress         = smart_address;
            smartContractBinary.object.byteCodeObjects  = code;
            smartContractBinary.object.instance         = state;
            smartContractBinary.stateCanModify          = solver_.isContractLocked(BlockChain::getAddressFromKey(smart_address)) ? true : false;
            if (auto optOriginRes = execute(address, smartContractBinary, methodHeader, isGetter)) {
                resp = optOriginRes.value().resp;
            }
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
            std::shared_lock lock(sharedErrorMutex_);
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
            std::shared_lock lock(sharedErrorMutex_);
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
            std::shared_lock lock(sharedErrorMutex_);
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
            std::shared_lock slk(sharedErrorMutex_);
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

    ~Executor() {
        stop();
    }

    static Executor& getInstance(const BlockChain* p_blockchain = nullptr, const cs::SolverCore* solver = nullptr, const int p_exec_port = 0,
        const std::string p_exec_ip = std::string{}, const std::string p_exec_cmdline = std::string{}) {  // singlton
        static Executor executor(*p_blockchain, *solver, p_exec_port, p_exec_ip, p_exec_cmdline);
        return executor;
    }

    void stop() {
        requestStop_ = true;
        // wake up watching thread if it sleeps
        cvErrorConnect_.notify_one();
    }

    std::optional<cs::Sequence> getSequence(const general::AccessID& accessId) {
        std::shared_lock lock(mutex_);
        if (auto it = accessSequence_.find(accessId); it != accessSequence_.end()) {
            return std::make_optional(it->second);
        }
        return std::nullopt;
    }

    std::optional<csdb::TransactionID> getDeployTrxn(const csdb::Address& p_address) {
        std::shared_lock lock(mutex_);
        if (const auto it = deployTrxns_.find(p_address); it != deployTrxns_.end()) {
            return std::make_optional(it->second);
        }
        return std::nullopt;
    }

    void updateDeployTrxns(const csdb::Address& p_address, const csdb::TransactionID& p_trxnsId) {
        std::lock_guard lock(mutex_);
        deployTrxns_[p_address] = p_trxnsId;
    }

    void setLastState(const csdb::Address& p_address, const std::string& p_state) {
        std::lock_guard lock(mutex_);
        lastState_[p_address] = p_state;
    }

    std::optional<std::string> getState(const csdb::Address& p_address) {
        csdb::Address abs_addr = blockchain_.getAddressByType(p_address, BlockChain::AddressType::PublicKey);
        if (!abs_addr.is_valid()) {
            return std::nullopt;
        }
        std::string state = cs::get_contract_state(abs_addr, blockchain_);
        if (state.empty()) {
            return std::nullopt;
        }
        return std::make_optional(std::move(state));
        //std::shared_lock lock(mutex_);
        //if (const auto it_last_state = lastState_.find(p_address); it_last_state != lastState_.end())
        //    return std::make_optional(it_last_state->second);
        //return std::nullopt;
    }

    void updateCacheLastStates(const csdb::Address& p_address, const cs::Sequence& sequence, const std::string& state) {
        std::lock_guard lock(mutex_);
        if (execCount_) {
            (cacheLastStates_[p_address])[sequence] = state;
        }
        else if (cacheLastStates_.size()) {
            cacheLastStates_.clear();
        }
    }

    std::optional<std::string> getAccessState(const general::AccessID& p_access_id, const csdb::Address& p_address) {
        std::shared_lock slk(mutex_);
        const auto access_sequence = getSequence(p_access_id);
        if (const auto unmap_states_it = cacheLastStates_.find(p_address); unmap_states_it != cacheLastStates_.end()) {
            std::pair<cs::Sequence, std::string> prev_seq_state{};
            for (const auto& [curr_seq, curr_state] : unmap_states_it->second) {
                if (curr_seq > access_sequence) {
                    return prev_seq_state.first ? std::make_optional<std::string>(prev_seq_state.second) : std::nullopt;
                }
                prev_seq_state = {curr_seq, curr_state};
            }
        }
        auto opt_last_sate = getState(p_address);
        return opt_last_sate.has_value() ? std::make_optional<std::string>(opt_last_sate.value()) : std::nullopt;
    }

    struct ExecuteResult {
    private:
        struct SmartRes {
            general::Variant retValue;
            std::string		 newState;
            // measured in milliseconds actual cost of execution
            int64_t			 executionCost;
            ::general::APIResponse response;
        };
    public:
        std::vector<SmartRes> smartsRes;
        std::map<csdb::Address, std::string> states;
        std::vector<csdb::Transaction> trxns;
        // measured in milliseconds total cost of executions
        long selfMeasuredCost;
        ::general::APIResponse response;
    };

    void addInnerSendTransaction(const general::AccessID& accessId, const csdb::Transaction& transaction) {
        std::lock_guard lk(mutex_);
        innerSendTransactions_[accessId].push_back(transaction);
    }

    std::optional<std::vector<csdb::Transaction>> getInnerSendTransactions(const general::AccessID& accessId) {
        std::shared_lock slk(mutex_);
        if (const auto it = innerSendTransactions_.find(accessId); it != innerSendTransactions_.end()) {
            return std::make_optional<std::vector<csdb::Transaction>>(it->second);
        }
        return std::nullopt;
    }

    void deleteInnerSendTransactions(const general::AccessID& accessId) {
        std::lock_guard lk(mutex_);
        innerSendTransactions_.erase(accessId);
    }

    bool isDeploy(const csdb::Transaction& trxn) {
        if (trxn.user_field(0).is_valid()) {
            const auto sci = deserialize<api::SmartContractInvocation>(trxn.user_field(0).value<std::string>());
            if (sci.method.empty()) {
                return true;
            }
        }
        return false;
    }

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

    enum ACCESS_ID_RESERVE { GETTER, START_INDEX };

    struct ExecuteTransactionInfo {
        // transaction to execute contract
        csdb::Transaction transaction;
        // pass method name convention
        MethodNameConvention convention;
        // max allowed fee
        csdb::Amount feeLimit;
    };

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

    std::optional<ExecuteResult> executeTransaction(const std::vector<ExecuteTransactionInfo>& smarts, std::string forceContractState) {
        static std::mutex mutex;
        std::lock_guard lock(mutex);  // temporary solution

        if (smarts.empty()) {
            return std::nullopt;
        }
        const auto& head_transaction = smarts[0].transaction;
        if (!head_transaction.is_valid()) {
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
        csdb::Transaction deployTrxn;
        const auto isdeploy = isDeploy(head_transaction);
        if (!isdeploy) {  // execute
            const auto optDeployId = getDeployTrxn(smartTarget);
            if (!optDeployId.has_value()) {
                return std::nullopt;
            }
            deployTrxn = blockchain_.loadTransaction(optDeployId.value());
        }
        else {
            deployTrxn = head_transaction;
        }

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

        const auto optOriginRes = execute(smartSource.to_api_addr(), smartContractBinary, methodHeader);

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

        const auto optInnerTransactions = getInnerSendTransactions(optOriginRes.value().acceessId);

        // fill res
        ExecuteResult res;
        res.response = optOriginRes.value().resp.status;

        if (optInnerTransactions.has_value()) {
            res.trxns = optInnerTransactions.value();
        }
        deleteInnerSendTransactions(optOriginRes.value().acceessId);
        //constexpr double FEE_IN_SEC = kMinFee * 4.0;
        //const double fee = std::max(kMinFee, static_cast<double>(optOriginRes.value().timeExecute) * FEE_IN_SEC);
        //res.fee = csdb::Amount(fee);
        res.selfMeasuredCost = (long) optOriginRes.value().timeExecute;
        for (const auto&[itAddress, itState] : optOriginRes.value().resp.externalContractsState) {
            auto addr = BlockChain::getAddressFromKey(itAddress);
            res.states[addr] = itState;
        }
        for (const auto& result : optOriginRes.value().resp.results) {
            res.smartsRes.push_back({ result.ret_val, result.invokedContractState, result.executionCost, result.status });
        }

        return res;
    }

    std::optional<ExecuteResult> reexecuteContract(ExecuteTransactionInfo& contract, std::string forceContractState) {
        static std::mutex mutex;
        std::lock_guard lock(mutex);  // temporary solution

        if (!contract.transaction.is_valid()) {
            return std::nullopt;
        }
        auto smartSource = blockchain_.getAddressByType(contract.transaction.source(), BlockChain::AddressType::PublicKey);
        auto smartTarget = blockchain_.getAddressByType(contract.transaction.target(), BlockChain::AddressType::PublicKey);

        // get deploy transaction
        csdb::Transaction deployTrxn;
        const auto isdeploy = isDeploy(contract.transaction);
        if (!isdeploy) {  // execute
            const auto optDeployId = getDeployTrxn(smartTarget);
            if (!optDeployId.has_value()) {
                return std::nullopt;
            }
            deployTrxn = blockchain_.loadTransaction(optDeployId.value());
        }
        else {
            deployTrxn = contract.transaction;
        }

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

        const auto optOriginRes = execute(smartSource.to_api_addr(), smartContractBinary, methodHeader);

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

        const auto optInnerTransactions = getInnerSendTransactions(optOriginRes.value().acceessId);

        // fill res
        ExecuteResult res;
        res.response = optOriginRes.value().resp.status;

        if (optInnerTransactions.has_value()) {
            res.trxns = optInnerTransactions.value();
        }
        deleteInnerSendTransactions(optOriginRes.value().acceessId);
        res.selfMeasuredCost = (long)optOriginRes.value().timeExecute;
        for (const auto& [itAddress, itState] : optOriginRes.value().resp.externalContractsState) {
            auto addr = BlockChain::getAddressFromKey(itAddress);
            res.states[addr] = itState;
        }
        for (const auto& result : optOriginRes.value().resp.results) {
            res.smartsRes.push_back({ result.ret_val, result.invokedContractState, result.executionCost, result.status });
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
        std::lock_guard lk(mutex_);
        lockSmarts[address] = accessId;
    }

    void deleteFromLockSmart(const general::Address& address, const general::AccessID& accessId) {
        csunused(accessId);
        std::lock_guard lk(mutex_);
        lockSmarts.erase(address);
    }

    bool isLockSmart(const general::Address& address, const general::AccessID& accessId) {
        std::lock_guard lk(mutex_);
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
    explicit Executor(const BlockChain& p_blockchain, const cs::SolverCore& solver, int p_exec_port,
        const std::string p_exec_ip, const std::string p_exec_cmdline)
    : blockchain_(p_blockchain)
    , solver_(solver)
    , executorTransport_(new ::apache::thrift::transport::TBufferedTransport(
        ::apache::thrift::stdcxx::make_shared<::apache::thrift::transport::TSocket>(p_exec_ip, p_exec_port)))
    , origExecutor_(
          std::make_unique<executor::ContractExecutorConcurrentClient>(::apache::thrift::stdcxx::make_shared<apache::thrift::protocol::TBinaryProtocol>(executorTransport_))) {
        std::thread th([=]() {
            std::string executor_cmdline = p_exec_cmdline;
            std::unique_ptr<cs::Process> executor_process;
            if(!executor_cmdline.empty()) {
                executor_process = std::make_unique<cs::Process>(executor_cmdline);
                executor_process->launch(cs::Process::Options::None);
            }
            while (true) {
                if (isConnect_) {
                    static std::mutex mt;
                    std::unique_lock ulk(mt);
                    cvErrorConnect_.wait(ulk, [&] { return !isConnect_ || requestStop_; });
                }

                if (requestStop_) {
                    break;
                }
                static const int RECONNECT_TIME = 10;
                std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_TIME));
                if (!executor_process || executor_process->isRunning()) {
                    if (connect())
                        disconnect();
                }
                else {
                    executor_process->launch(cs::Process::Options::None);
                }
            }
            if (executor_process) {
                executor_process->terminate();
            }
        });
        th.detach();
    }

    struct OriginExecuteResult {
        ExecuteByteCodeResult resp;
        general::AccessID acceessId;
        // measured execution duration in milliseconds
        long long timeExecute;
    };

    uint64_t generateAccessId() {
        std::lock_guard lk(mutex_);
        ++lastAccessId_;
        accessSequence_[lastAccessId_] = blockchain_.getLastSequence();
        return lastAccessId_;
    }

    uint64_t getFutureAccessId() {
        return lastAccessId_ + 1;
    }

    void deleteAccessId(const general::AccessID& p_access_id) {
        std::lock_guard lk(mutex_);
        accessSequence_.erase(p_access_id);
    }

	std::optional<OriginExecuteResult> execute(const std::string& address, const SmartContractBinary& smartContractBinary, std::vector<MethodHeader>& methodHeader, bool isGetter = false) {
        constexpr uint64_t EXECUTION_TIME = Consensus::T_smart_contract;
        OriginExecuteResult originExecuteRes{};
        if (!connect()) {
            return std::nullopt;
        }

        uint64_t access_id{};
        if (!isGetter)
            access_id = generateAccessId();

        //const auto access_id = generateAccessId();
        ++execCount_;
        const auto timeBeg = std::chrono::steady_clock::now();
        try {
            std::shared_lock lock(sharedErrorMutex_);
            origExecutor_->executeByteCode(originExecuteRes.resp, access_id, address, smartContractBinary, methodHeader, EXECUTION_TIME, EXECUTOR_VERSION);
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
        originExecuteRes.timeExecute = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - timeBeg).count();
        --execCount_;
        if (!isGetter)
            deleteAccessId(access_id);
        disconnect();
        originExecuteRes.acceessId = access_id;
        return std::make_optional<OriginExecuteResult>(std::move(originExecuteRes));
    }

    bool connect() {
        try {
            if (executorTransport_->isOpen()) {
                executorTransport_->close();
            }

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
        try {
            executorTransport_->close();
        }
        catch (::apache::thrift::transport::TTransportException&) {
            isConnect_ = false;
            cvErrorConnect_.notify_one();
        }
    }

    //
    using OriginExecutor = executor::ContractExecutorConcurrentClient;
    using BinaryProtocol = apache::thrift::protocol::TBinaryProtocol;
    std::shared_mutex sharedErrorMutex_;
    void reCreationOriginExecutor() {
        std::lock_guard glk(sharedErrorMutex_);
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

    std::shared_mutex mutex_;
    std::atomic_size_t execCount_{0};

    std::condition_variable cvErrorConnect_;
    std::atomic_bool isConnect_{ false };
    std::atomic_bool requestStop_{ false };
    const uint16_t EXECUTOR_VERSION = 1;
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
    void PoolGet(PoolGetResult& _return, const int64_t sequence) override;

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

    void ExecuteCountGet(ExecuteCountGetResult& _return, const std::string& executeMethod) override;
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
    void TokensListGet(api::TokensListResult&, int64_t offset, int64_t limit, const TokensListSortField order, const bool desc, const std::string& filterName, const std::string& filterCode) override;
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
        std::string lastRetVal;
        //csdb::TransactionID transaction;
        csdb::TransactionID initer;
    };

    using smart_state_entry = cs::WorkerQueue<SmartState>;
    using client_type = executor::ContractExecutorConcurrentClient;

    BlockChain& s_blockchain;
    cs::SolverCore& solver;
#ifdef MONITOR_NODE
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
    cs::SpinLockable<std::map<csdb::Address, smart_state_entry>> smart_state;
    cs::SpinLockable<std::map<csdb::Address, smart_trxns_queue>> smart_last_trxn;

    cs::SpinLockable<std::map<csdb::Address, std::vector<csdb::TransactionID>>> deployed_by_creator;
    cs::SpinLockable<PendingSmartTransactions> pending_smart_transactions;
    std::map<csdb::PoolHash, api::Pool> poolCache;
    std::atomic_flag state_updater_running = ATOMIC_FLAG_INIT;
    std::thread state_updater;

    std::map<std::string, int64_t> mExecuteCount_;

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

    // the method implements common part of both update_smart_caches_once() and update_smart_caches_slot() methods
    template<typename LongNamedType>
    bool update_smart_caches(LongNamedType& locked_pending_smart_transactions, bool init);

    bool update_smart_caches_once(const csdb::PoolHash&, bool = false);
    void run();

    ::csdb::Transaction make_transaction(const ::api::Transaction&);
    void dumb_transaction_flow(api::TransactionFlowResult& _return, const ::api::Transaction&);
    void smart_transaction_flow(api::TransactionFlowResult& _return, const ::api::Transaction&);

    TokensMaster tm;

    const uint8_t ERROR_CODE = 1;

    friend class ::csconnector::connector;

    std::condition_variable_any newBlockCv_;
    std::mutex dbLock_;

private slots:
    void update_smart_caches_slot(const csdb::Pool& pool);
    void update_smart_state_slot(const csdb::Transaction& tr_new_state);
    void store_block_slot(const csdb::Pool& pool);
    void collect_all_stats_slot(const csdb::Pool& pool);
};
}  // namespace api

bool is_deploy_transaction(const csdb::Transaction& tr);
bool is_smart(const csdb::Transaction& tr);
bool is_smart_state(const csdb::Transaction& tr);
bool is_smart_deploy(const api::SmartContractInvocation& smart);

#endif  // APIHANDLER_HPP
