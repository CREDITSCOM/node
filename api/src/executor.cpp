#include <executor.hpp>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include <thrift/transport/TBufferTransports.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "serializer.hpp"

#include <csnode/configholder.hpp>

#include <solver/solvercore.hpp>
#include <solver/smartcontracts.hpp>

void cs::ExecutorSettings::set(cs::Reference<const BlockChain> blockchain, cs::Reference<const cs::SolverCore> solver) {
    blockchain_ = blockchain;
    solver_ = solver;
}

cs::ExecutorSettings::Types cs::ExecutorSettings::get() {
    auto tuple = std::make_tuple(std::any_cast<cs::Reference<const BlockChain>>(blockchain_),
                                 std::any_cast<cs::Reference<const cs::SolverCore>>(solver_));

    blockchain_.reset();
    solver_.reset();

    return tuple;
}

void cs::Executor::executeByteCode(executor::ExecuteByteCodeResult& resp, const std::string& address, const std::string& smart_address,
                                   const std::vector<general::ByteCodeObject>& code, const std::string& state,
                                   std::vector<executor::MethodHeader>& methodHeader, bool isGetter, cs::Sequence sequence) {
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

void cs::Executor::executeByteCodeMultiple(executor::ExecuteByteCodeMultipleResult& _return, const general::Address& initiatorAddress,
                                           const executor::SmartContractBinary& invokedContract, const std::string& method,
                                           const std::vector<std::vector<general::Variant>>& params, const int64_t executionTime, cs::Sequence sequence) {
    if (!isConnected()) {
        _return.status.code = 1;
        _return.status.message = "No executor connection!";

        notifyError();
        return;
    }

    const auto accessId = generateAccessId(sequence);
    ++execCount_;

    try {
        std::shared_lock lock(sharedErrorMutex_);
        origExecutor_->executeByteCodeMultiple(_return, static_cast<general::AccessID>(accessId), initiatorAddress, invokedContract, method, params, executionTime, EXECUTOR_VERSION);
    }
    catch (::apache::thrift::transport::TTransportException& x) {
        // sets stop_ flag to true forever, replace with new instance
        if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
            recreateOriginExecutor();
        }

        _return.status.code = 1;
        _return.status.message = x.what();

        notifyError();
    }
    catch (std::exception& x) {
        _return.status.code = 1;
        _return.status.message = x.what();

        notifyError();
    }

    --execCount_;
    deleteAccessId(static_cast<general::AccessID>(accessId));
}

void cs::Executor::getContractMethods(executor::GetContractMethodsResult& _return, const std::vector<general::ByteCodeObject>& byteCodeObjects) {
    try {
        std::shared_lock lock(sharedErrorMutex_);
        origExecutor_->getContractMethods(_return, byteCodeObjects, EXECUTOR_VERSION);
    }
    catch (const ::apache::thrift::transport::TTransportException& x) {
        // sets stop_ flag to true forever, replace with new instance
        if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
            recreateOriginExecutor();
        }

        _return.status.code = 1;
        _return.status.message = x.what();

        notifyError();
    }
    catch(const std::exception& x ) {
        _return.status.code = 1;
        _return.status.message = x.what();

        notifyError();
    }
}

void cs::Executor::getContractVariables(executor::GetContractVariablesResult& _return, const std::vector<general::ByteCodeObject>& byteCodeObjects, const std::string& contractState) {
    try {
        std::shared_lock lock(sharedErrorMutex_);
        origExecutor_->getContractVariables(_return, byteCodeObjects, contractState, EXECUTOR_VERSION);
    }
    catch (const ::apache::thrift::transport::TTransportException& x) {
        // sets stop_ flag to true forever, replace with new instance
        if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
            recreateOriginExecutor();
        }

        _return.status.code = 1;
        _return.status.message = x.what();

        notifyError();
    }
    catch(const std::exception& x ) {
        _return.status.code = 1;
        _return.status.message = x.what();

        notifyError();
    }
}

void cs::Executor::compileSourceCode(executor::CompileSourceCodeResult& _return, const std::string& sourceCode) {
    try {
        std::shared_lock slk(sharedErrorMutex_);
        origExecutor_->compileSourceCode(_return, sourceCode, EXECUTOR_VERSION);
    }
    catch (::apache::thrift::transport::TTransportException& x) {
        // sets stop_ flag to true forever, replace with new instance
        if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
            recreateOriginExecutor();
        }

        _return.status.code = 1;
        _return.status.message = x.what();

        notifyError();
    }
    catch(const std::exception& x ) {
        _return.status.code = 1;
        _return.status.message = x.what();

        notifyError();
    }
}

void cs::Executor::getExecutorBuildVersion(executor::ExecutorBuildVersionResult& _return) {
    try {
        std::shared_lock slk(sharedErrorMutex_);
        origExecutor_->getExecutorBuildVersion(_return, EXECUTOR_VERSION);
    }
    catch (::apache::thrift::transport::TTransportException& x) {
        // sets stop_ flag to true forever, replace with new instance
        if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
            recreateOriginExecutor();
        }

        _return.status.code = 1;
        _return.status.message = x.what();

        notifyError();
    }
    catch (const std::exception& x) {
        _return.status.code = 1;
        _return.status.message = x.what();

        notifyError();
    }
}

cs::Executor& cs::Executor::instance() {
    static Executor executor(cs::ExecutorSettings::get());
    return executor;
}

bool cs::Executor::isConnected() const {
    return executorTransport_->isOpen();
}

void cs::Executor::stop() {
    requestStop_ = true;

    while (isWatcherRunning_.load(std::memory_order_acquire)) {
        notifyError(); // wake up watching thread if it sleeps
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (executorProcess_) {
        if (executorProcess_->isRunning()) {
            disconnect();
            executorProcess_->terminate();
        }
    }

    if (manager_.isExecutorProcessRunning()) {
        manager_.stopExecutorProcess();
    }
}

std::optional<cs::Sequence> cs::Executor::getSequence(const general::AccessID& accessId) {
    std::shared_lock lock(mutex_);

    if (auto it = accessSequence_.find(accessId); it != accessSequence_.end()) {
        return std::make_optional(it->second);
    }

    return std::nullopt;
}

std::optional<csdb::TransactionID> cs::Executor::getDeployTrxn(const csdb::Address& address) {
    std::shared_lock lock(mutex_);

    if (const auto it = deployTrxns_.find(address); it != deployTrxns_.end()) {
        return std::make_optional(it->second);
    }

    return std::nullopt;
}

void cs::Executor::updateDeployTrxns(const csdb::Address& address, const csdb::TransactionID& trxnsId) {
    std::lock_guard lock(mutex_);
    deployTrxns_[address] = trxnsId;
}

void cs::Executor::setLastState(const csdb::Address& address, const std::string& state) {
    std::lock_guard lock(mutex_);
    lastState_[address] = state;
}

std::optional<std::string> cs::Executor::getState(const csdb::Address& address) {
    csdb::Address absAddress = blockchain_.getAddressByType(address, BlockChain::AddressType::PublicKey);

    if (!absAddress.is_valid()) {
        return std::nullopt;
    }

    std::string state = cs::SmartContracts::get_contract_state(blockchain_, absAddress);

    if (state.empty()) {
        return std::nullopt;
    }

    return std::make_optional(std::move(state));
}

void cs::Executor::updateCacheLastStates(const csdb::Address& address, const cs::Sequence& sequence, const std::string& state) {
    std::lock_guard lock(mutex_);

    if (execCount_) {
        (cacheLastStates_[address])[sequence] = state;
    }
    else if (cacheLastStates_.size()) {
        cacheLastStates_.clear();
    }
}

std::optional<std::string> cs::Executor::getAccessState(const general::AccessID& accessId, const csdb::Address& address) {
    std::shared_lock lock(mutex_);
    const auto accessSequence = getSequence(accessId);

    if (const auto unmapStatesIter = cacheLastStates_.find(address); unmapStatesIter != cacheLastStates_.end()) {
        std::pair<cs::Sequence, std::string> prevSeqState{};

        for (const auto& [currSeq, currState] : unmapStatesIter->second) {
            if (currSeq > accessSequence) {
                return prevSeqState.first ? std::make_optional<std::string>(prevSeqState.second) : std::nullopt;
            }

            prevSeqState = { currSeq, currState };
        }
    }

    auto lastState = getState(address);
    return lastState.has_value() ? std::make_optional<std::string>(std::move(lastState).value()) : std::nullopt;
}

void cs::Executor::addInnerSendTransaction(const general::AccessID& accessId, const csdb::Transaction& transaction) {
    std::lock_guard lock(mutex_);
    innerSendTransactions_[accessId].push_back(transaction);
}

std::optional<std::vector<csdb::Transaction>> cs::Executor::getInnerSendTransactions(const general::AccessID& accessId) {
    std::shared_lock lock(mutex_);

    if (const auto it = innerSendTransactions_.find(accessId); it != innerSendTransactions_.end()) {
        return std::make_optional(it->second);
    }

    return std::nullopt;
}

void cs::Executor::deleteInnerSendTransactions(const general::AccessID& accessId) {
    std::lock_guard lock(mutex_);
    innerSendTransactions_.erase(accessId);
}

bool cs::Executor::isDeploy(const csdb::Transaction& transaction) {
    if (transaction.user_field(0).is_valid()) {
        const auto sci = cs::Serializer::deserialize<api::SmartContractInvocation>(transaction.user_field(0).value<std::string>());

        if (sci.method.empty()) {
            return true;
        }
    }

    return false;
}

std::optional<cs::Executor::ExecuteResult> cs::Executor::executeTransaction(const std::vector<cs::Executor::ExecuteTransactionInfo>& smarts, std::string forceContractState) {
    if (smarts.empty()) {
        return std::nullopt;
    }

    const auto& headTransaction = smarts[0].transaction;
    const auto& deployTrxn = smarts[0].deploy;

    if (!headTransaction.is_valid() || !deployTrxn.is_valid()) {
        return std::nullopt;
    }

    // all smarts must have the same initiator and address
    const auto source = headTransaction.source();
    const auto target = headTransaction.target();

    for (const auto& smart : smarts) {
        if (source != smart.transaction.source() || target != smart.transaction.target()) {
            return std::nullopt;
        }
    }

    auto smartSource = blockchain_.getAddressByType(source, BlockChain::AddressType::PublicKey);
    auto smartTarget = blockchain_.getAddressByType(target, BlockChain::AddressType::PublicKey);

    // get deploy transaction
    const auto isdeploy = (headTransaction.id() == deployTrxn.id()); //isDeploy(head_transaction);

    // fill smartContractBinary
    const auto sciDeploy = cs::Serializer::deserialize<api::SmartContractInvocation>(deployTrxn.user_field(cs::trx_uf::deploy::Code).value<std::string>());
    executor::SmartContractBinary smartContractBinary;
    smartContractBinary.contractAddress = smartTarget.to_api_addr();
    smartContractBinary.object.byteCodeObjects = sciDeploy.smartContractDeploy.byteCodeObjects;

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

    smartContractBinary.stateCanModify = solver_.isContractLocked(smartTarget);

    // fill methodHeaders
    std::vector<executor::MethodHeader> methodHeaders;
    for (const auto& smartItem : smarts) {
        executor::MethodHeader header;
        const csdb::Transaction& smart = smartItem.transaction;

        if (smartItem.convention != MethodNameConvention::Default) {
            // call to payable
            // add method name
            header.methodName = "payable";

            // add arg[0]
            general::Variant& var0 = header.params.emplace_back(::general::Variant{});
            std::string str_val = smart.amount().to_string();

            if (smartItem.convention == MethodNameConvention::PayableLegacy) {
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

            if (smartItem.convention == MethodNameConvention::PayableLegacy) {
                var1.__set_v_string(str_val);
            }
            else {
                var1.__set_v_byte_array(str_val);
            }
        }
        else {
            api::SmartContractInvocation sci;
            const auto fld = smart.user_field(cs::trx_uf::start::Methods);

            if (!fld.is_valid()) {
                return std::nullopt;
            }
            else if (!isdeploy) {
                sci = cs::Serializer::deserialize<api::SmartContractInvocation>(fld.value<std::string>());
                header.methodName = sci.method;
                header.params = sci.params;

                for (const auto& addrLock : sci.usedContracts) {
                    addToLockSmart(addrLock, static_cast<general::AccessID>(getFutureAccessId()));
                }
            }
        }

        methodHeaders.push_back(header);
    }

    const auto optOriginRes = execute(smartSource.to_api_addr(), smartContractBinary, methodHeaders, false /*isGetter*/, smarts[0].sequence /*sequence*/, headTransaction.get_time());

    for (const auto& smart : smarts) {
        if (!isdeploy) {
            if (smart.convention == MethodNameConvention::Default) {
                const auto fld = smart.transaction.user_field(0);
                if (fld.is_valid()) {
                    auto sci = cs::Serializer::deserialize<api::SmartContractInvocation>(smart.transaction.user_field(0).value<std::string>());
                    for (const auto& addrLock : sci.usedContracts) {
                        deleteFromLockSmart(addrLock, static_cast<general::AccessID>(getFutureAccessId()));
                    }
                }
            }
        }
    }

    if (!optOriginRes.has_value()) {
        return std::nullopt;
    }

    // fill res
    ExecuteResult res;
    res.response = optOriginRes.value().resp.status;

    deleteInnerSendTransactions(optOriginRes.value().acceessId);
    res.selfMeasuredCost = static_cast<long>(optOriginRes.value().timeExecute);

    for (const auto& setters : optOriginRes.value().resp.results) {
        auto& smartRes = res.smartsRes.emplace_back(ExecuteResult::SmartRes{});
        smartRes.retValue = setters.ret_val;
        smartRes.executionCost = setters.executionCost;
        smartRes.response = setters.status;

        for (auto& states : setters.contractsState) {  // state
            auto addr = BlockChain::getAddressFromKey(states.first);
            smartRes.states[BlockChain::getAddressFromKey(states.first)] = states.second;
        }

        for (auto transaction : setters.emittedTransactions) {  // emittedTransactions
            ExecuteResult::EmittedTrxn emittedTrxn;
            emittedTrxn.source = BlockChain::getAddressFromKey(transaction.source);
            emittedTrxn.target = BlockChain::getAddressFromKey(transaction.target);
            emittedTrxn.amount = csdb::Amount(transaction.amount.integral, static_cast<uint64_t>(transaction.amount.fraction));
            emittedTrxn.userData = transaction.userData;
            smartRes.emittedTransactions.push_back(emittedTrxn);
        }
    }

    return std::make_optional(std::move(res));
}

std::optional<cs::Executor::ExecuteResult> cs::Executor::reexecuteContract(cs::Executor::ExecuteTransactionInfo& contract, std::string forceContractState) {
    if (!contract.transaction.is_valid() || !contract.deploy.is_valid()) {
        return std::nullopt;
    }

    auto smartSource = blockchain_.getAddressByType(contract.transaction.source(), BlockChain::AddressType::PublicKey);
    auto smartTarget = blockchain_.getAddressByType(contract.transaction.target(), BlockChain::AddressType::PublicKey);

    // get deploy transaction
    const csdb::Transaction& deployTrxn = contract.deploy;
    const auto isdeploy = (contract.deploy.id() == contract.transaction.id()); // isDeploy(contract.transaction);

    // fill smartContractBinary
    const auto sci_deploy = cs::Serializer::deserialize<api::SmartContractInvocation>(deployTrxn.user_field(0).value<std::string>());
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

    // fill methodHeaders
    std::vector<executor::MethodHeader> methodHeaders;
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
            sci = cs::Serializer::deserialize<api::SmartContractInvocation>(fld.value<std::string>());
            header.methodName = sci.method;
            header.params = sci.params;

            for (const auto& addrLock : sci.usedContracts) {
                addToLockSmart(addrLock, static_cast<general::AccessID>(getFutureAccessId()));
            }
        }
    }

    methodHeaders.push_back(header);

    const auto optOriginRes = execute(smartSource.to_api_addr(), smartContractBinary, methodHeaders, false /*! isGetter*/, contract.sequence);

    if (!isdeploy) {
        if (contract.convention == MethodNameConvention::Default) {
            const auto fld = contract.transaction.user_field(0);
            if (fld.is_valid()) {
                auto sci = cs::Serializer::deserialize<api::SmartContractInvocation>(contract.transaction.user_field(0).value<std::string>());
                for (const auto& addrLock : sci.usedContracts) {
                    deleteFromLockSmart(addrLock, static_cast<general::AccessID>(getFutureAccessId()));
                }
            }
        }
    }

    if (!optOriginRes.has_value()) {
        return std::nullopt;
    }

    // fill res
    ExecuteResult res;
    res.response = optOriginRes.value().resp.status;

    deleteInnerSendTransactions(optOriginRes.value().acceessId);
    res.selfMeasuredCost = static_cast<long>(optOriginRes.value().timeExecute);

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
            emittedTrxn.amount = csdb::Amount(transaction.amount.integral, static_cast<uint64_t>(transaction.amount.fraction));
            emittedTrxn.userData = transaction.userData;
            smartRes.emittedTransactions.push_back(emittedTrxn);
        }
    }

    return std::make_optional(std::move(res));
}

csdb::Transaction cs::Executor::makeTransaction(const api::Transaction& transaction) {
    csdb::Transaction sendTransaction;
    const auto source = BlockChain::getAddressFromKey(transaction.source);
    const uint64_t walletDenom = csdb::Amount::AMOUNT_MAX_FRACTION;  // 1'000'000'000'000'000'000ull;

    sendTransaction.set_amount(csdb::Amount(transaction.amount.integral, uint64_t(transaction.amount.fraction), walletDenom));

    BlockChain::WalletData dummy{};

    if (!blockchain_.findWalletData(source, dummy)) {
        return csdb::Transaction{}; // disable transaction from unknown source!
    }

    sendTransaction.set_currency(csdb::Currency(1));
    sendTransaction.set_source(source);
    sendTransaction.set_target(BlockChain::getAddressFromKey(transaction.target));
    sendTransaction.set_max_fee(csdb::AmountCommission(uint16_t(transaction.fee.commission)));
    sendTransaction.set_innerID(transaction.id & 0x3fffffffffff);

    // TODO Change Thrift to avoid copy
    cs::Signature signature;
    if (transaction.signature.size() == signature.size()) {
        std::copy(transaction.signature.begin(), transaction.signature.end(), signature.begin());
    }
    else {
        signature.fill(0);
    }

    sendTransaction.set_signature(signature);
    return sendTransaction;
}

void cs::Executor::stateUpdate(const csdb::Pool& pool) {
    if (!pool.transactions().size()) {
        return;
    }

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

void cs::Executor::addToLockSmart(const general::Address& address, const general::AccessID& accessId) {
    std::lock_guard lock(mutex_);
    lockSmarts_[address] = accessId;
}

void cs::Executor::deleteFromLockSmart(const general::Address& address, const general::AccessID& accessId) {
    csunused(accessId);
    std::lock_guard lock(mutex_);
    lockSmarts_.erase(address);
}

bool cs::Executor::isLockSmart(const general::Address& address, const general::AccessID& accessId) {
    std::lock_guard lock(mutex_);

    if (auto addrLock = lockSmarts_.find(address); addrLock != lockSmarts_.end() && addrLock->second == accessId) {
        return true;
    }

    return false;
}

csdb::Transaction cs::Executor::loadTransactionApi(const csdb::TransactionID& id) const {
    std::lock_guard lock(blockMutex_);
    return blockchain_.loadTransaction(id);
}

uint64_t cs::Executor::getTimeSmartContract(general::AccessID accessId) {
    std::lock_guard lock(mutex_);   
    if (auto it = executeTrxnsTime.find(accessId); it != executeTrxnsTime.end()) 
        return it->second;
    return 0;
}

void cs::Executor::onBlockStored(const csdb::Pool& pool) {
    stateUpdate(pool);
}

void cs::Executor::onReadBlock(const csdb::Pool& pool) {
    stateUpdate(pool);
}

void cs::Executor::onExecutorStarted() {
    if (!isConnected()) {
        connect();
    }

    csdebug() << csname() << "started";
}

void cs::Executor::onExecutorFinished(int code, const std::error_code&) {
    if (requestStop_) {
        return;
    }

    if (!executorMessages_.count(code)) {
        cswarning() << "Executor unknown error";
    }
    else {
        cswarning() << executorMessages_[code];
    }

    if (code == ExecutorErrorCode::ServerStartError ||
        code == ExecutorErrorCode::IncorrecJdkVersion) {
        return;
    }

    notifyError();
}

void cs::Executor::onExecutorProcessError(const cs::ProcessException& exception) {
    cswarning() << "Executor process error occured " << exception.what() << ", code " << exception.code();
}

void cs::Executor::checkExecutorVersion() {
    executor::ExecutorBuildVersionResult _return;
    bool result = false;

    std::this_thread::sleep_for(std::chrono::milliseconds(cs::ConfigHolder::instance().config()->getApiSettings().executorCheckVersionDelay));

    if (requestStop_) {
        return;
    }

    connect();
    getExecutorBuildVersion(_return);

    if (_return.status.code != 0) {
        cserror() << "Start contract executor error code " << int(_return.status.code) << ": " << _return.status.message;
    }
    else {
        cslog() << "Start contract executor: " << _return.status.message << ". Executor build number " << _return.commitNumber;
    }

    result = _return.commitNumber < commitMin_ || (_return.commitNumber > commitMax_ && commitMax_ != -1);
    csdebug() << "[executorInfo]: commitNumber: " << _return.commitNumber << ", commitHash: " << _return.commitHash;

    if (result) {
        if (commitMax_ != -1) {
            cserror() << "Executor commit number: " << _return.commitNumber << " is out of range (" << commitMin_ << " .. " << commitMax_ << ")";
        }
        else {
            cserror() << "Executor commit number: " << _return.commitNumber << " is out of range (" << commitMin_ << " .. any)";
        }

        auto terminate = [this] {
            executorProcess_->terminate();
            notifyError();
        };

        cs::Concurrent::run(terminate, cs::ConcurrentPolicy::Thread);
    }
}

cs::Executor::Executor(const cs::ExecutorSettings::Types& types)
: blockchain_(std::get<cs::Reference<const BlockChain>>(types))
, solver_(std::get<cs::Reference<const cs::SolverCore>>(types))
, socket_(::apache::thrift::stdcxx::make_shared<::apache::thrift::transport::TSocket>(cs::ConfigHolder::instance().config()->getApiSettings().executorHost,
                                                                                      cs::ConfigHolder::instance().config()->getApiSettings().executorPort))
, executorTransport_(new ::apache::thrift::transport::TBufferedTransport(socket_))
, origExecutor_(std::make_unique<executor::ContractExecutorConcurrentClient>(::apache::thrift::stdcxx::make_shared<apache::thrift::protocol::TBinaryProtocol>(executorTransport_))) {
    socket_->setSendTimeout(cs::ConfigHolder::instance().config()->getApiSettings().executorSendTimeout);
    socket_->setRecvTimeout(cs::ConfigHolder::instance().config()->getApiSettings().executorReceiveTimeout);

    commitMin_ = cs::ConfigHolder::instance().config()->getApiSettings().executorCommitMin;
    commitMax_ = cs::ConfigHolder::instance().config()->getApiSettings().executorCommitMax;

    if (cs::ConfigHolder::instance().config()->getApiSettings().executorCmdLine.empty()) {
        cswarning() << "Executor command line args are empty, process would not be created";
        return;
    }

    executorProcess_ = std::make_unique<cs::Process>(cs::ConfigHolder::instance().config()->getApiSettings().executorCmdLine);

    cs::Connector::connect(&executorProcess_->started, this, &Executor::onExecutorStarted);
    cs::Connector::connect(&executorProcess_->finished, this, &Executor::onExecutorFinished);
    cs::Connector::connect(&executorProcess_->errorOccured, this, &Executor::onExecutorProcessError);
    cs::Connector::connect(&executorProcess_->started, this, &Executor::checkExecutorVersion);

    checkAnotherExecutor();
    executorProcess_->launch(cs::Process::Options::None);

    std::this_thread::sleep_for(std::chrono::milliseconds(cs::ConfigHolder::instance().config()->getApiSettings().executorRunDelay));
    state_ = executorProcess_->isRunning() ? ExecutorState::Launched : ExecutorState::Idle;

    if (state_ == ExecutorState::Idle) {
        cswarning() << "Executor can not start, watcher thread would not be created";
        return;
    }

    auto watcher = [this]() {
        isWatcherRunning_.store(true, std::memory_order_release);

        while (!requestStop_) {
            if (isConnected()) {
                static std::mutex mutex;
                std::unique_lock lock(mutex);

                cvErrorConnect_.wait_for(lock, std::chrono::seconds(5), [&] {
                    return !isConnected() || requestStop_;
                });
            }

            if (requestStop_) {
                break;
            }

            if (executorProcess_->isRunning()) {
                if (!isConnected()) {
                    connect();
                }
            }
            else if (state_ != ExecutorState::Launching) {
                checkAnotherExecutor();
                runProcess();
            }
        }

        isWatcherRunning_.store(false, std::memory_order_release);
        cslog() << "Executor watcher thread finished";
    };

    cs::Concurrent::run(watcher, cs::ConcurrentPolicy::Thread);
}

cs::Executor::~Executor() {
    if (!requestStop_) {
        stop();
    }
}

void cs::Executor::runProcess() {
    state_ = ExecutorState::Launching;

    executorProcess_->terminate();

    std::this_thread::sleep_for(std::chrono::milliseconds(cs::ConfigHolder::instance().config()->getApiSettings().executorRunDelay));

    executorProcess_->setProgram(cs::ConfigHolder::instance().config()->getApiSettings().executorCmdLine);
    executorProcess_->launch(cs::Process::Options::None);

    state_ = ExecutorState::Launched;
}

void cs::Executor::checkAnotherExecutor() {
    if (!cs::ConfigHolder::instance().config()->getApiSettings().executorMultiInstance) {
        manager_.stopExecutorProcess();
    }
}

uint64_t cs::Executor::generateAccessId(cs::Sequence explicitSequence, uint64_t time) {
    std::lock_guard lock(mutex_);
    ++lastAccessId_;
    accessSequence_[lastAccessId_] = (explicitSequence != kUseLastSequence ? explicitSequence : blockchain_.getLastSeq());

    if(time)
        executeTrxnsTime[lastAccessId_] = time;

    return static_cast<uint64_t>(lastAccessId_);
}

uint64_t cs::Executor::getFutureAccessId() {
    return static_cast<uint64_t>(lastAccessId_ + 1);
}

void cs::Executor::deleteAccessId(const general::AccessID& accessId) {
    std::lock_guard lock(mutex_);
    accessSequence_.erase(accessId);
    executeTrxnsTime.erase(accessId);
}

std::optional<cs::Executor::OriginExecuteResult> cs::Executor::execute(const std::string& address, const executor::SmartContractBinary& smartContractBinary,
                                                                       std::vector<executor::MethodHeader>& methodHeader, bool isGetter, cs::Sequence explicitSequence, uint64_t time) {
    constexpr uint64_t EXECUTION_TIME = Consensus::T_smart_contract;
    OriginExecuteResult originExecuteRes{};

    if (!isConnected()) {
        notifyError();
        return std::nullopt;
    }

    uint64_t accessId{};

    if (!isGetter) {
        accessId = generateAccessId(explicitSequence, time);
    }

    ++execCount_;

    const auto timeBeg = std::chrono::steady_clock::now();

    try {
        std::shared_lock sharedLock(sharedErrorMutex_);
        std::lock_guard lock(callExecutorLock_);
        origExecutor_->executeByteCode(originExecuteRes.resp, static_cast<general::AccessID>(accessId), address, smartContractBinary, methodHeader, EXECUTION_TIME, EXECUTOR_VERSION);
    }
    catch (::apache::thrift::transport::TTransportException& x) {
        // sets stop_ flag to true forever, replace with new instance
        if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
            recreateOriginExecutor();
        }

        if (x.getType() == ::apache::thrift::transport::TTransportException::TIMED_OUT) {
            originExecuteRes.resp.status.code = cs::error::TimeExpired;
        }
        else {
            originExecuteRes.resp.status.code = cs::error::ThriftException;
        }
        originExecuteRes.resp.status.message = x.what();

        notifyError();
    }
    catch (std::exception& x) {
        originExecuteRes.resp.status.code = cs::error::StdException;
        originExecuteRes.resp.status.message = x.what();

        notifyError();
    }

    originExecuteRes.timeExecute = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - timeBeg).count();
    --execCount_;

    if (!isGetter) {
        deleteAccessId(static_cast<general::AccessID>(accessId));
    }

    originExecuteRes.acceessId = static_cast<general::AccessID>(accessId);
    return std::make_optional(std::move(originExecuteRes));
}

bool cs::Executor::connect() {
    try {
        executorTransport_->open();
    }
    catch (...) {
        notifyError();
    }

    return executorTransport_->isOpen();
}

void cs::Executor::disconnect() {
    try {
        executorTransport_->close();
    }
    catch (::apache::thrift::transport::TTransportException&) {
        notifyError();
    }
}

void cs::Executor::notifyError() {
    if (isConnected()) {
        disconnect();
    }

    cvErrorConnect_.notify_one();
}

void cs::Executor::recreateOriginExecutor() {
    std::lock_guard lock(sharedErrorMutex_);
    disconnect();
    origExecutor_.reset(new executor::ContractExecutorConcurrentClient(::apache::thrift::stdcxx::make_shared<apache::thrift::protocol::TBinaryProtocol>(executorTransport_)));
}
