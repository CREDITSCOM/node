#include <smartcontracts.hpp>
#include <solvercontext.hpp>

#include <ContractExecutor.h>
#include <base58.h>

#include <cscrypto/cryptoconstants.hpp>

#include <csdb/currency.hpp>

#include <csnode/configholder.hpp>
#include <csnode/datastream.hpp>
#include <csnode/transactionsiterator.hpp>
#include <csnode/eventreport.hpp>
#include <csnode/fee.hpp>

#include <lib/system/logger.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <sstream>

#include <serializer.hpp>

namespace {
    const char* kLogPrefix = "Smart: ";

    inline void print(std::ostream& os, const ::general::Variant& var) {
        os << "Variant(";
        bool print_default = false;
        if (var.__isset.v_string) {
            os << var.v_string;
        }
        else if (var.__isset.v_null) {
            os << "Null";
        }
        else if (var.__isset.v_boolean) {
            os << var.v_boolean;
        }
        else if (var.__isset.v_boolean_box) {
            os << var.v_boolean_box;
        }
        else if (var.__isset.v_array) {
            os << "Array";
        }
        else if (var.__isset.v_object) {
            os << "Object";
        }
        else if (var.__isset.v_void) {
            os << "Void";
        }
        else if (var.__isset.v_list) {
            os << "List";
        }
        else if (var.__isset.v_set) {
            os << "Set";
        }
        else if (var.__isset.v_map) {
            os << "Map";
        }
        else if (var.__isset.v_int) {
            os << var.v_int;
        }
        else if (var.__isset.v_int_box) {
            os << var.v_int_box;
        }
        else if (var.__isset.v_byte) {
            os << (unsigned int)var.v_byte;
        }
        else if (var.__isset.v_byte_box) {
            os << (unsigned int)var.v_byte_box;
        }
        else if (var.__isset.v_short) {
            os << var.v_short;
        }
        else if (var.__isset.v_short_box) {
            os << var.v_short_box;
        }
        else if (var.__isset.v_long) {
            os << var.v_long;
        }
        else if (var.__isset.v_long_box) {
            os << var.v_long_box;
        }
        else if (var.__isset.v_float) {
            os << var.v_float;
        }
        else if (var.__isset.v_float_box) {
            os << var.v_float_box;
        }
        else if (var.__isset.v_double) {
            os << var.v_double;
        }
        else if (var.__isset.v_double_box) {
            os << var.v_double_box;
        }
        else if (var.__isset.v_big_decimal) {
            os << var.v_big_decimal;
        }
        else if (var.__isset.v_byte_array) {
            os << "byte[" << var.v_byte_array.size() << ']';
        }
        else {
            /* other variant types are shown by default */
            //print_default = true;
            os << "unset";
        }
        os << ')';

        if (print_default) {
            os << ": ";
            var.printTo(os);
        }
    }

    // serializes val passed to special transaction user field new_state::RetVal
    inline void set_return_value(csdb::Transaction& new_state_transaction, const ::general::Variant& val) {
        new_state_transaction.add_user_field(cs::trx_uf::new_state::RetVal, cs::Serializer::serialize(val));
    }

    inline void set_return_value(csdb::Transaction& new_state_transaction, int8_t val) {
        ::general::Variant variant;
        variant.__set_v_byte(val);
        set_return_value(new_state_transaction, variant);
    }

}

namespace cs {

csdb::UserField SmartContractRef::to_user_field() const {
    cs::Bytes data;
    cs::ODataStream stream(data);
    stream << csdb::PoolHash{} /*for compatibility*/ << sequence << transaction;
    return csdb::UserField(stream.convert<std::string>());
}

void SmartContractRef::from_user_field(const csdb::UserField& fld) {
    std::string data = fld.value<std::string>();
    cs::IDataStream stream(data.c_str(), data.size());
    csdb::PoolHash dummy; // for compatibility
    stream >> dummy >> sequence >> transaction;
    if (!stream.isValid() || stream.isAvailable(1)) {
        cserror() << "SmartCotractRef: failed to read from malformed user field";
        invalidate();
    }
}

void SmartContracts::QueueItem::add(const SmartContractRef& ref_contract, csdb::Transaction tr_start, bool to_end) {
    csdb::Amount tr_start_fee = csdb::Amount(tr_start.counted_fee().to_double());
    // TODO: here new_state_fee prediction may be calculated, currently it is equal to starter fee
    csdb::Amount new_state_fee = csdb::Amount(cs::fee::getContractStateMinFee().to_double());
    // apply starter fee consumed
    csdb::Amount avail_fee = csdb::Amount(tr_start.max_fee().to_double()) - tr_start_fee - new_state_fee;
    //consumed_fee = 0;
    ExecutionItem e{ ref_contract, tr_start.clone(), avail_fee, new_state_fee, csdb::Amount{ 0 }, {}, {} };
    auto& execution = to_end ? executions.emplace_back(e) : *executions.emplace(executions.cbegin(), e);

    if (SmartContracts::is_executable(tr_start)) {
        const csdb::UserField fld = tr_start.user_field(trx_uf::start::Methods);  // start::Methods == deploy::Code, so does not matter what type of executable is
        if (fld.is_valid()) {
            std::string data = fld.value<std::string>();
            if (!data.empty()) {
                auto invoke = cs::Serializer::deserialize<api::SmartContractInvocation>(std::move(data));
                if (!invoke.usedContracts.empty()) {
                    for (const auto item : invoke.usedContracts) {
                        if (item.size() == cscrypto::kPublicKeySize) {
                            const csdb::Address addr = csdb::Address::from_public_key(item.c_str()); // BlockChain::getAddressFromKey(item);
                            if (addr.is_valid()) {
                                execution.uses.push_back(addr);
                            }
                        }                        
                    }
                }
            }
        }
    }
    // reserve new_state fee for every using contract also
    if (!execution.uses.empty()) {
        for (const auto& it : execution.uses) {
            csunused(it);
            execution.avail_fee -= new_state_fee;
        }
    }
}

/*explicit*/
SmartContracts::SmartContracts(BlockChain& blockchain, CallsQueueScheduler& calls_queue_scheduler)
: scheduler(calls_queue_scheduler)
, bc(blockchain)
, executor_ready(true)
, max_read_sequence(0)
{
}

SmartContracts::~SmartContracts() = default;

/*public*/
void SmartContracts::init(const cs::PublicKey& id, Node* node) {
    cs::Lock lock(public_access_lock);

    pnode = node;
    auto connector_ptr = pnode->getConnector();
    if (connector_ptr != nullptr) {
        exec_handler_ptr = connector_ptr->apiExecHandler();
    }
    node_id = id;
}

/*public*/
void SmartContracts::subscribeToSignals(Node* node) {
    // signals subscription (MUST occur AFTER the BlockChains has already subscribed to storage)
    // as event receiver:
    cs::Connector::connect(&bc.startReadingBlocksEvent(), this, &SmartContracts::on_start_reading_blocks);
    cs::Connector::connect(&bc.readBlockEvent(), this, &SmartContracts::on_read_block);
    cs::Connector::connect(&bc.storeBlockEvent, this, &SmartContracts::on_store_block);
    cs::Connector::connect(&bc.removeBlockEvent, this, &SmartContracts::on_remove_block);
    cs::Connector::connect(&cs::Conveyer::instance().statesCreated, this, &SmartContracts::on_update);
    // as event source:
    cs::Connector::connect(&signal_payable_invoke, &bc, &BlockChain::onPayableContractReplenish);
    cs::Connector::connect(&signal_contract_timeout, &bc, &BlockChain::onContractTimeout);
    cs::Connector::connect(&signal_emitted_accepted, &bc, &BlockChain::onContractEmittedAccepted);
    cs::Connector::connect(&rollback_payable_invoke, &bc, &BlockChain::rollbackPayableContractReplenish);
    cs::Connector::connect(&rollback_contract_timeout, &bc, &BlockChain::rollbackContractTimeout);
    cs::Connector::connect(&rollback_emitted_accepted, &bc, &BlockChain::rollbackContractEmittedAccepted);

    // Node's signal
    if (node) {
        cs::Connector::connect(&node->gotRejectedContracts, this, &SmartContracts::on_reject);
    }
}

/*static*/
std::string SmartContracts::get_error_message(int8_t code) {
    using namespace cs::error;
    switch (code) {
    case ContractError:
        return "error in contract";
    case TimeExpired:
        return "timeout during operation";
    case OutOfFunds:
        return "insufficient funds to complete operation";
    case StdException:
        return "connection error while executing contract";
    case Exception:
        return "common error while executing contract";
    case UnpayableReplenish:
        return "replenished contract does not implement payable()";
    case ConsensusRejected:
        return "the trusted consensus have rejected new_state (or emitted transactions)";
    case ExecuteTransaction:
        return "common error in executor";
    case InternalBug:
        return "internal bug in node detected";
    case ExecutorUnreachable:
    case ThriftException:
    case NodeUnreachable:
        return "executor is disconnected or unavailable, or incompatible";
    case LogicViolation:
        return "logic violation, execution result breaks the rules";
    case ExecutorIncompatible:
        return "incompatible executor version";
    }
    std::ostringstream os;
    os << "Error code " << (int)code;
    return os.str();
}

/*static*/
bool SmartContracts::is_smart_contract(const csdb::Transaction& tr) {
    return SmartContracts::is_executable(tr) || SmartContracts::is_new_state(tr);
}

/*static*/
bool SmartContracts::is_executable(const csdb::Transaction& tr) {
    if (!tr.is_valid()) {
        return false;
    }
    // to contain smart contract trx must contain either FLD[0] (deploy, start) or FLD[-2] (new_state), both of type
    // "String":
    csdb::UserField f = tr.user_field(trx_uf::deploy::Code);
    if constexpr (trx_uf::deploy::Code != trx_uf::start::Methods) {
        if (!f.is_valid()) {
            f = tr.user_field(trx_uf::start::Methods);
        }
    }
    return (f.is_valid() && f.type() == csdb::UserField::Type::String);
}

/*static*/
bool SmartContracts::is_deploy(const csdb::Transaction& tr) {
    if (!SmartContracts::is_executable(tr)) {
        return false;
    }

    using namespace cs::trx_uf;
    csdb::UserField uf = tr.user_field(deploy::Code);
    if (!uf.is_valid()) {
        return false;
    }

    const auto invoke = cs::Serializer::deserialize<api::SmartContractInvocation>(uf.value<std::string>());
    // deploy ~ start but method in invoke info is empty
    return invoke.method.empty();
}

/*static*/
bool SmartContracts::is_start(const csdb::Transaction& tr) {
    return SmartContracts::is_executable(tr) && !SmartContracts::is_deploy(tr);
}

/*static*/
bool SmartContracts::is_new_state(const csdb::Transaction& tr) {
    if (!tr.is_valid()) {
        return false;
    }
    // must contain user field new_state::Value and new_state::RefStart
    using namespace cs::trx_uf;
    // test user_field[RefStart] helps filter out ancient smart contracts:
    if (tr.user_field(new_state::RefStart).type() != csdb::UserField::Type::String) {
        return false;
    }
    return (tr.user_field(new_state::Value).type() == csdb::UserField::Type::String || tr.user_field(new_state::Hash).type() == csdb::UserField::Type::String);
}

// true if tr is new_state and contract state is updated
/*static*/
bool SmartContracts::is_state_updated(const csdb::Transaction& tr) {
    if (!SmartContracts::is_new_state(tr)) {
        return false;
    }
    csdb::UserField fld = tr.user_field(trx_uf::new_state::Value);
    if (fld.is_valid()) {
        return !fld.value<std::string>().empty();
    }
    fld = tr.user_field(trx_uf::new_state::Hash);
    if (fld.is_valid()) {
        cs::Hash hash;
        std::string tmp = fld.value< std::string >();
        if (tmp.size() == hash.size()) {
            std::copy(tmp.cbegin(), tmp.cend(), hash.begin());
            return !(hash == cs::Zero::hash);
        }
    }
    csdebug() << kLogPrefix << "incorrect new_state transaction detected, contains neither state value nor state hash";
    return false;
}

/* static */
/* Assuming deployer.is_public_key() */
csdb::Address SmartContracts::get_valid_smart_address(const csdb::Address& deployer, const uint64_t trId, const api::SmartContractDeploy& data) {
    static_assert(cscrypto::kHashSize <= cscrypto::kPublicKeySize);
    const uint8_t kInnerIdSize = 6;

    std::vector<cscrypto::Byte> strToHash;
    std::string byteCode{};
    if (!data.byteCodeObjects.empty()) {
        for (auto& curr_byteCode : data.byteCodeObjects) {
            byteCode += curr_byteCode.byteCode;
        }
    }
    strToHash.reserve(cscrypto::kPublicKeySize + kInnerIdSize + byteCode.size());

    const auto dPk = deployer.public_key();
    const auto idPtr = reinterpret_cast<const cscrypto::Byte*>(&trId);

    std::copy(dPk.begin(), dPk.end(), std::back_inserter(strToHash));
    std::copy(idPtr, idPtr + kInnerIdSize, std::back_inserter(strToHash));
    std::copy(byteCode.begin(), byteCode.end(), std::back_inserter(strToHash));

    cscrypto::Hash hash = cscrypto::calculateHash(strToHash.data(), strToHash.size());
    cscrypto::PublicKey res;
    res.fill(0);
    std::copy(hash.data(), hash.data() + cscrypto::kHashSize, res.data());

    return csdb::Address::from_public_key(res);
}

/*static*/
csdb::Transaction SmartContracts::get_transaction(const BlockChain& storage, const SmartContractRef& contract) {
    csdb::Pool block = storage.loadBlock(contract.sequence);
    if (!block.is_valid()) {
        return csdb::Transaction{};
    }
    if (contract.transaction >= block.transactions_count()) {
        return csdb::Transaction{};
    }
    return block.transactions().at(contract.transaction);
}

/*static*/
csdb::Transaction SmartContracts::get_transaction(const BlockChain& storage, const csdb::Transaction& state_transaction) {
    if (!SmartContracts::is_new_state(state_transaction)) {
        return csdb::Transaction{};
    }
    csdb::UserField fld = state_transaction.user_field(trx_uf::new_state::RefStart);
    if (!fld.is_valid()) {
        return csdb::Transaction{};
    }
    SmartContractRef ref(fld);
    return SmartContracts::get_transaction(storage, ref);
}

/*static*/
std::string SmartContracts::get_contract_state(const BlockChain& storage, const csdb::Address& abs_addr) {
    SmartContractRef dummy;
    std::string state;
    // no matter which value is returned:
    /*bool ok =*/ SmartContracts::dbcache_read(storage, abs_addr, dummy, state);
    return state;
}

/*static*/
std::string SmartContracts::to_base58(const BlockChain& storage, const csdb::Address& addr) {
    csdb::Address abs_addr = storage.getAddressByType(addr, BlockChain::AddressType::PublicKey);
    const cs::PublicKey& key = abs_addr.public_key();
    return EncodeBase58(key.data(), key.data() + key.size());
}

std::optional<api::SmartContractInvocation> SmartContracts::find_deploy_info(const csdb::Address& abs_addr) const {
    using namespace trx_uf;
    const auto item = known_contracts.find(abs_addr);
    if (item != known_contracts.cend()) {
        const StateItem& val = item->second;
        if (val.ref_deploy.is_valid()) {
            csdb::Transaction tr_deploy = get_transaction(val.ref_deploy, abs_addr);
            if (tr_deploy.is_valid()) {
                csdb::UserField fld = tr_deploy.user_field(deploy::Code);
                if (fld.is_valid()) {
                    std::string data = fld.value<std::string>();
                    if (!data.empty()) {
                        return std::make_optional(cs::Serializer::deserialize<api::SmartContractInvocation>(std::move(data)));
                    }
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<api::SmartContractInvocation> SmartContracts::get_smart_contract_impl(const csdb::Transaction& tr) {
    // currently calls to is_***() from this method are prohibited, infinite recursion is possible!
    using namespace trx_uf;

    bool is_replenish_contract = false;
    if (!is_smart_contract(tr)) {
        is_replenish_contract = is_payable_target(tr);
        if (!is_replenish_contract) {
            return std::nullopt;
        }
    }

    const csdb::Address abs_addr = absolute_address(tr.target());

    // get info from private contracts table (faster), not from API

    if (is_new_state(tr) || is_replenish_contract) {
        auto maybe_contract = find_deploy_info(abs_addr);
        if (maybe_contract.has_value()) {
            return maybe_contract;
        }
    }
    // is executable (deploy or start):
    else {
        const csdb::UserField fld = tr.user_field(deploy::Code);  // start::Methods == deploy::Code, so does not matter what type of executable is
        if (fld.is_valid()) {
            std::string data = fld.value<std::string>();
            if (!data.empty()) {
                auto invoke = cs::Serializer::deserialize<api::SmartContractInvocation>(std::move(data));
                if (invoke.method.empty()) {
                    // is deploy
                    return std::make_optional(std::move(invoke));
                }
                else {
                    // is start
                    auto maybe_deploy = find_deploy_info(abs_addr);
                    if (maybe_deploy.has_value()) {
                        api::SmartContractInvocation& deploy = maybe_deploy.value();
                        deploy.method = invoke.method;
                        deploy.params = invoke.params;
                        return std::make_optional(deploy);
                    }
                }
            }
        }
    }

    return std::nullopt;
}

bool SmartContracts::is_payable_target(const csdb::Transaction& tr) {
    csdb::Address abs_addr = absolute_address(tr.target());
    if (!in_known_contracts(abs_addr)) {
        return false;
    }
    // may do blocking call to API::executor
    return is_payable(abs_addr);
}

/*public*/
csdb::Transaction SmartContracts::get_contract_call(const csdb::Transaction& contract_state) const {
    cs::Lock lock(public_access_lock);

    csdb::UserField fld = contract_state.user_field(trx_uf::new_state::RefStart);
    if (fld.is_valid()) {
        SmartContractRef ref(fld);
        return get_transaction(ref, absolute_address(contract_state.target()));
    }
    return csdb::Transaction{};
}

/*public*/
csdb::Transaction SmartContracts::get_contract_deploy(const csdb::Address& addr) const {
    cs::Lock lock(public_access_lock);

    const csdb::Address abs_addr = absolute_address(addr);
    return get_deploy_transaction(abs_addr);
}

csdb::Transaction SmartContracts::get_transaction(const SmartContractRef& contract, const csdb::Address& abs_addr) const {
    queue_const_iterator it_queue = find_in_queue(contract);
    if (it_queue != exe_queue.cend()) {
        execution_const_iterator it_exe = find_in_queue_item(it_queue, contract);
        if (it_exe != it_queue->executions.cend()) {
            if (it_exe->ref_start == contract) {
                return it_exe->transaction;
            }
        }
    }
    auto it_state = known_contracts.find(abs_addr);
    if (it_state != known_contracts.cend()) {
        const auto& item = it_state->second;
        if (item.ref_execute == contract && item.execute.is_valid()) {
            return item.execute;
        }
        if (item.ref_deploy == contract && item.deploy.is_valid()) {
            return item.deploy;
        }
    }
    return SmartContracts::get_transaction(bc, contract);
}

csdb::Transaction SmartContracts::get_deploy_transaction(const csdb::Address& abs_addr) const {
    auto it_state = known_contracts.find(abs_addr);
    if (it_state != known_contracts.cend()) {
        const auto& contract = it_state->second;
        if (contract.deploy.is_valid()) {
            return contract.deploy;
        }
    }
    return csdb::Transaction{};
}

void SmartContracts::enqueue(const csdb::Pool& block, size_t trx_idx, bool skip_log) {
    if (trx_idx >= block.transactions_count()) {
        cserror() << kLogPrefix << "incorrect trx index in block to enqueue smart contract";
        return;
    }
    SmartContractRef new_item(block.sequence(), trx_idx);
    csdb::Transaction t = block.transaction(trx_idx);
    csdb::Address abs_addr = absolute_address(t.target());

    auto it = find_in_queue(new_item);
    if (it != exe_queue.cend()) {
        if (!skip_log) {
            csdebug() << kLogPrefix << "attempt to queue duplicated " << FormatRef(new_item.sequence, new_item.transaction)
                << ", already queued on round #" << it->seq_enqueue;
        }
        return;
    }

    // test if this contract has already enqueued in this block
    for (it = exe_queue.begin(); it != exe_queue.end(); ++it) {
        if (it->seq_enqueue == new_item.sequence && it->abs_addr == abs_addr) {
            break;
        }
    }

    [[maybe_unused]] bool payable = false;
    if (it == exe_queue.end()) {
        // enqueue to end
        if (SmartContracts::is_deploy(t)) {
            // pre-register in known_contracts, metadata is not actual
            auto maybe_invoke_info = get_smart_contract_impl(t);
            if (maybe_invoke_info.has_value()) {
                const auto& invoke_info = maybe_invoke_info.value();
                StateItem& state = known_contracts[abs_addr];
                state.ref_deploy = new_item;
                state.deploy = t.clone();
                if (update_metadata(invoke_info, state, skip_log)) {
                    payable = implements_payable(state.payable);
                }
            }
        }
        else {
            // "lazy" metadata update, also covers cases of reading contracts from DB
            if (!is_metadata_actual(abs_addr)) {
                auto maybe_invoke_info = get_smart_contract_impl(t);
                if (maybe_invoke_info.has_value()) {
                    StateItem& state = known_contracts[abs_addr];
                    update_metadata(maybe_invoke_info.value(), state, skip_log);
                }
            }
            payable = is_payable(abs_addr);
        }
        if (!skip_log) {
            cslog() << kLogPrefix << "enqueue " << print_executed_method(t);
        }
        it = exe_queue.emplace(exe_queue.cend(), QueueItem(new_item, abs_addr, t));
    }
    else {
        // add to existing queue item
        it->add(new_item, t, true /*at_end*/);
        if (!skip_log) {
            cslog() << kLogPrefix << "add " << new_item << " to already enqueued contract";
        }
    }

    if (!it->executions.empty()) {
        execution_iterator execution = find_in_queue_item(it, new_item);
        if (execution == it->executions.end()) {
            // smth. strange, failed to find newly created item
            // nothing to do with it
            csdebug() << kLogPrefix << "(logical error) unable to find just created execution item";
        }
        else {
            // in addition to contract "subcalls" set by transaction take more from contract's metadata
            const std::string method = get_executed_method_name(t);
            const size_t cnt_0 = execution->uses.size();
            add_uses_from(abs_addr, method, execution->uses);  // if failed, executor_ready wil be set to false
            // and from explicit uses from starter transaction (applicable to payable() calls)
            size_t cnt_m = execution->uses.size();
            csdb::UserField fld = t.user_field(cs::trx_uf::ordinary::UsedContracts);
            if (fld.is_valid()) {
                if (payable) {
                    std::string extra_uses_list = fld.value<std::string>();
                    if (!extra_uses_list.empty()) {
                        auto total_size = extra_uses_list.size();
                        for (size_t offset = 0; offset + cscrypto::kPublicKeySize <= total_size; offset += cscrypto::kPublicKeySize) {
                            cs::PublicKey key;
                            std::copy(extra_uses_list.data() + offset, extra_uses_list.data() + offset + cscrypto::kPublicKeySize, key.begin());
                            execution->uses.emplace_back(csdb::Address::from_public_key(key));
                        }
                    }
                }
                else {
                    csdebug() << kLogPrefix << "(logical error) explicit used contracts set for non-replenish call, or contract does not implement payable() method";
                }
            }

            const size_t cnt = execution->uses.size();
            if (cnt > 0) {
                for (const auto& u : execution->uses) {
                    if (cnt_m > 0) {
                        // added from metadata item
                        --cnt_m;
                        if (!skip_log) {
                            csdebug() << kLogPrefix << new_item << " uses " << to_base58(u) << " from contract metadata";
                        }
                    }
                    else {
                        // explicitly added by replenish transaction
                        if (!skip_log) {
                            csdebug() << kLogPrefix << new_item << " uses " << to_base58(u) << " from replenish transaction";
                        }
                    }
                    if (!in_known_contracts(u)) {
                        csdebug() << kLogPrefix << "call to unknown contract " << to_base58(u) << " declared in " << new_item << ", cancel ";
                        remove_from_queue(new_item, skip_log);
                        // also removes parent "it" from exe_queue if empty
                        return;
                    }
                }
            }
            if (cnt > cnt_0) {
                for (size_t i = cnt_0; i < cnt; ++i) {
                    execution->avail_fee -= execution->new_state_fee;  // reserve more fee for future new_state
                }
            }
            execution->consumed_fee += smart_round_fee(block);  // setup costs of initial round, 0 actually
        }
    }

    update_status(*it, new_item.sequence, SmartContractStatus::Waiting, skip_log);
    it->is_executor = contains_me(block.confidants());
}

void SmartContracts::test_exe_queue(bool reading_db) {
    // update queue items status
    auto it = exe_queue.begin();
    while (it != exe_queue.end()) {
        if (it->status == SmartContractStatus::Canceled) {
            if (!reading_db) {
                csdebug() << kLogPrefix << "finished " << FormatRef(it->seq_enqueue) << " still in queue, remove it";
            }
            it = remove_from_queue(it, reading_db);
            continue;
        }
        if (it->executions.empty()) {
            // the senseless item in the queue
            if (!reading_db) {
                csdebug() << kLogPrefix << "empty " << FormatRef(it->seq_enqueue) << " in queue, remove it";
            }
            it = remove_from_queue(it, reading_db);
            continue;
        }
        if (it->status == SmartContractStatus::Running) {
            // some contract is already running
            ++it;
            continue;
        }
        if (it->status == SmartContractStatus::Finished) {
            // some contract is under consensus
            ++it;
            continue;
        }
        // status: Waiting or Idle

        // is locked:
        bool wait_until_unlock = false;
        if (is_locked(it->abs_addr)) {
            if (!reading_db) {
                csdetails() << kLogPrefix << FormatRef(it->seq_enqueue) << " still is locked, wait until unlocked";
            }
            wait_until_unlock = true;
        }
        // is anyone of using locked:
        else {
            for (const auto& execution : it->executions) {
                for (const auto& u : execution.uses) {
                    if (is_locked(absolute_address(u))) {
                        if (!reading_db) {
                            csdetails() << kLogPrefix << "some contract using by "
                                << FormatRef(execution.ref_start.sequence, execution.ref_start.transaction) << " still is locked, wait until unlocked";
                        }
                        wait_until_unlock = true;
                        break;
                    }
                }
            }
        }
        if (wait_until_unlock) {
            ++it;
            continue;
        }

        if (!reading_db) {
            csdebug() << kLogPrefix << "set running status to " << FormatRef(it->seq_enqueue) << " containing " << it->executions.size() << " jobs";
        }
        update_status(*it, bc.getLastSeq(), SmartContractStatus::Running, reading_db);

        if (!reading_db) {
            // call to executor only if is trusted relatively to this contract
            if (it->is_executor || cs::ConfigHolder::instance().config()->alwaysExecuteContracts()) {
                // final decision to execute contract is here, based on executor availability
                if (it->is_executor && !executor_ready && !test_executor_availability()) {
                    cslog() << kLogPrefix << "skip " << FormatRef(it->seq_enqueue) << ", execution is not allowed (executor is not connected)";
                    it->is_executor = false;
                    // notify partners that unable to play trusted role
                    bool fake_sent = false;
                    const auto& confidants = pnode->retriveSmartConfidants(it->seq_enqueue);
                    for (auto itconf = confidants.cbegin(); itconf != confidants.cend(); ++itconf) {
                        if (std::equal(itconf->cbegin(), itconf->cend(), node_id.cbegin())) {
                            cslog() << kLogPrefix << "unable to execute " << FormatRef(it->seq_enqueue) << ", so send fake stage-1 & stage-2";
                            cs::Byte own_conf_num = cs::Byte(itconf - confidants.cbegin());
                            // empty it->executions tested above, so it is safe to call to front()
                            const auto& ref_start = it->executions.front().ref_start;
                            uint64_t id = SmartConsensus::createId(ref_start.sequence, uint16_t(ref_start.transaction), 0);
                            SmartConsensus::sendFakeStageOne(pnode, confidants, own_conf_num, id);
                            SmartConsensus::sendFakeStageTwo(pnode, confidants, own_conf_num, id);
                            fake_sent = true;
                            break;
                        }
                    }
                    if (!fake_sent) {
                        cslog() << kLogPrefix << "unable to execute " << FormatRef(it->seq_enqueue) << " and failed to send fake stage-1 & stage-2";
                    }
                }
                else {
                    if (!executor_ready) {
                        // ask user to restart executor every 2 seconds
                        if (!wait_until_executor(1 /*sec*/, 1 /*try only once*/)) {
                            cserror() << kLogPrefix << "cannot connect to executor, unable call to contract";
                        }
                        else {
                            executor_ready = true;
                        }
                    }
                    if (executor_ready) {
                        csdebug() << kLogPrefix << "execute " << FormatRef(it->seq_enqueue) << " now";
                        execute_async(it->executions);
                    }
                }
            }
            else {
                csdebug() << kLogPrefix << "skip " << FormatRef(it->seq_enqueue) << " execution, not in trusted list";
            }
        } // under !reading_db block

        ++it;
    }
}

SmartContractStatus SmartContracts::get_smart_contract_status(const csdb::Address& addr) const {
    if (!exe_queue.empty()) {
        const auto it = find_first_in_queue(absolute_address(addr));
        if (it != exe_queue.cend()) {
            return it->status;
        }
    }
    return SmartContractStatus::Idle;
}

/*public*/
bool SmartContracts::executionAllowed() {
    cs::Lock lock(public_access_lock);

    if (!executor_ready) {
        if (!wait_until_executor(2 /*seconds, period*/, 1 /*max requests*/)) {
            cserror() << kLogPrefix << "cannot connect to executor, further blockchain reading is impossible, interrupt reading";
            if (pnode->isStopRequested()) {
                cslog() << kLogPrefix << "node is requested to stop, cancel wait to executor";
            }
            return false;
        }
        executor_ready = true;
    }
    return executor_ready;
}

/*public*/
uint32_t SmartContracts::test_violations(const csdb::Transaction& tr) {
    cs::Sequence last_block_seq = bc.getLastSeq();

    cs::Lock lock(public_access_lock);

    uint32_t result = Violations::None;

    if (SmartContracts::is_executable(tr) || is_payable_target(tr)) {
        const double avail_fee = tr.max_fee().to_double() - cs::fee::getFee(tr).to_double();
        if (avail_fee - cs::fee::getContractStateMinFee().to_double() < std::numeric_limits<double>::epsilon()) {
            csdebug() << kLogPrefix << "insufficient max fee in start transaction, prevalidation failed";
            result += Violations::InsufficientMaxFee;
        }
    }

    // test smart contract as source of transaction
    bool is_emitted = false;
    csdb::Address abs_addr = absolute_address(tr.source());
    if (in_known_contracts(abs_addr)) {
        is_emitted = !SmartContracts::is_new_state(tr);
    }

    // test smart contract as target of transaction (is it payable?)
    abs_addr = absolute_address(tr.target());
    bool is_contract = false;
    bool has_state = false;
    const auto it = known_contracts.find(abs_addr);
    if (it != known_contracts.end()) {
        is_contract = true;
        has_state = !it->second.state.empty();
    }

    if (is_contract) {
        bool is_invocation = false;

        // test contract calls contract
        if (is_emitted) {
            csdebug() << kLogPrefix << "contract is not allowed call to other contract, drop it";
            result += Violations::SourceIsContract;
        }
        // test contract was deployed (and maybe called successfully)
        if (!has_state) {
            // test possible uncompleted deploy
            bool violation_confirmed = false;
            if (it->second.ref_deploy.is_valid() && it->second.ref_state.is_valid() && it->second.ref_state.sequence <= last_block_seq) {
                // deploy has finished with empty contract state
                violation_confirmed = true;
            }
            else if (it->second.deploy.is_valid()) {
                cs::Sequence deploy_seq = it->second.deploy.id().pool_seq();
                if ((last_block_seq > deploy_seq) && (last_block_seq - deploy_seq > Consensus::MaxRoundsCancelContract)) {
                    // deploy timeout confirmed
                    violation_confirmed = true;
                }
            }
            if (violation_confirmed) {
                cslog() << kLogPrefix << "unable execute not successfully deployed contract, drop transaction";
                result += Violations::ContractIsNotDeployed;
            }
        }

        api::SmartContractInvocation invoke;
        if (is_executable(tr)) {
            csdb::UserField fld = tr.user_field(trx_uf::start::Methods);
            if constexpr (trx_uf::deploy::Code != trx_uf::start::Methods) {
                // in case of user field number in call to contract other then deploy contract
                if (!fld.is_valid()) {
                    fld = tr.user_field(trx_uf::deploy::Code);
                }
            }
            if (fld.is_valid()) {
                std::string data = fld.value<std::string>();
                if (!data.empty()) {
                    try {
                        invoke = cs::Serializer::deserialize<api::SmartContractInvocation>(std::move(data));
                        is_invocation = true;
                    }
                    catch (::apache::thrift::protocol::TProtocolException&) {
                    }
                }
            }
            if (!is_invocation) {
                result += Violations::BadInvoke;
            }
        }
        // test against subsequent contract calls
        if (is_invocation) {
            if (!invoke.usedContracts.empty()) {
                result += Violations::SubsequentCall;
            }
            else {
                std::vector<csdb::Address> uses;
                add_uses_from(abs_addr, invoke.method, uses);
                if (!uses.empty()) {
                    result += Violations::SubsequentCall;
                }
            }
            if (invoke.__isset.smartContractDeploy) {
                // deploy contract
                if (!invoke.method.empty()) {
                    // must not combine call info with deploy info
                    result += Violations::MalformedCall;
                }
                if (invoke.smartContractDeploy.sourceCode.empty()) {
                    // source code is absent in deploy info
                    result += Violations::MalformedCall;
                }
            }
        }

        double amount = tr.amount().to_double();
        // possible blocking call to executor for the first time:
        if (!is_payable(abs_addr)) {
            if (amount > std::numeric_limits<double>::epsilon()) {
                cslog() << kLogPrefix << "unable replenish balance of contract without payable() feature, drop transaction";
                result += Violations::ReplenishNonPayable;
            }
            else /*amount is 0*/ {
                if (!is_smart_contract(tr)) {
                    // not deploy/execute/new_state transaction as well as smart is not payable
                    cslog() << kLogPrefix << "unable call to payable(), feature is not implemented in contract, drop transaction";
                    result += Violations::ReplenishNonPayable;
                }
            }
        }
        else /* is payable */ {
            // test if payable() is not directly called
            if (is_invocation) {
                if (invoke.method == PayableName) {
                    cslog() << kLogPrefix << "unable call to payable() directly, drop transaction";
                    result += Violations::DirectCallToPayable;
                }
            }
            else /* not executable transaction */ {
                // contract is payable and transaction addresses it, ok then
            }
        }
    }

    return result;
}

/*static*/
std::string SmartContracts::violations_message(uint32_t flags) {
    if (flags == Violations::None) {
        return "no violations";
    }
    std::ostringstream os;
    if ((flags & Violations::SourceIsContract) != 0) {
        os << "Contract is not allowed to emit transaction via API. ";
    }
    if ((flags & Violations::ContractIsNotDeployed) != 0) {
        os << "Contract is not deployed successfully. ";
    }
    if ((flags & Violations::ReplenishNonPayable) != 0) {
        os << "Cannot replenish unpayable contract. ";
    }
    if ((flags & Violations::DirectCallToPayable) != 0) {
        os << "Unable perform direct call to payable(). ";
    }
    if ((flags & Violations::BadInvoke) != 0) {
        os << "Malformed invoke info. ";
    }
    if ((flags & Violations::SubsequentCall) != 0) {
        os << "Unable call contract from other contract. ";
    }
    if ((flags & Violations::MalformedCall) != 0) {
        os << "Malformed contract execution info";
    }
    if ((flags & Violations::InsufficientMaxFee) != 0) {
        os << "Insufficient max fee to call contract";
    }
    if ((flags & Violations::InsufficientCallerFunds) != 0) {
        os << "Insufficient caller balance";
    }
    return os.str();
}

/*static*/
Reject::Reason SmartContracts::prevalidate(const BlockChain& bc, const cs::TransactionsPacket& pack) {
    // count: 0 < count < max
    const auto total_cnt = pack.transactionsCount();
    if (total_cnt > Consensus::MaxContractResultTransactions) {
        csdebug() << kLogPrefix << "too many transactions in execute result, prevalidation failed";
        return Reject::Reason::LimitExceeded;
    }
    if (total_cnt < 1) {
        csdebug() << kLogPrefix << "no transactions in execute result, prevalidation failed";
        return Reject::Reason::MalformedTransaction;
    }

    // logical rules:
    // a) strong packet structure, state[0] + {emitted[0][0]..emitted[0][N0]} opt + ... + state[N] + {emitted[N][0]..emitted[N][NN]} opt
    // b) emitted source == previous new state source
    // c) source != target in emitted transactions
    // d) no executions in contract emitted transactions
    csdb::Address contract_abs_addr;
    double total_min_fee = 0;
    size_t i = 0;
    size_t total_size = 0;
    for (const auto& t : pack.transactions()) {
        const auto size = t.to_byte_stream().size();
        if (size > Consensus::MaxTransactionSize) {
            csdebug() << kLogPrefix << "exceeded max transaction size, prevalidation failed";
            return Reject::Reason::LimitExceeded;
        }
        total_size += size;
        if (size > Consensus::MaxPreliminaryBlockSize) {
            csdebug() << kLogPrefix << "exceeded max block size, prevalidation failed";
            return Reject::Reason::LimitExceeded;
        }

        if (SmartContracts::is_new_state(t)) {
            contract_abs_addr = bc.getAddressByType(t.source(), BlockChain::AddressType::PublicKey);
            total_min_fee += cs::fee::getContractStateMinFee().to_double();
        }
        else {
            if (i == 0) {
                csdebug() << kLogPrefix << "execute result does not start with new state, prevalidation failed";
                return Reject::Reason::MalformedTransaction;
            }
            const csdb::Address src_abs_addr = bc.getAddressByType(t.source(), BlockChain::AddressType::PublicKey);
            if (!(src_abs_addr == contract_abs_addr)) {
                csdebug() << kLogPrefix << "incorrect source in emitted transaction, prevalidation failed";
                return Reject::Reason::MalformedContractAddress;
            }
            const csdb::Address tgt_abs_addr = bc.getAddressByType(t.target(), BlockChain::AddressType::PublicKey);
            if (src_abs_addr == tgt_abs_addr) {
                csdebug() << kLogPrefix << "source equals target in emitted transaction, prevalidation failed";
                return Reject::Reason::MalformedTransaction;
            }
            if (SmartContracts::is_executable(t)) {
                csdebug() << kLogPrefix << "execute transaction in execute result, prevalidation failed";
                return Reject::Reason::MalformedTransaction;
            }
            total_min_fee += cs::fee::getFee(t).to_double();// getContractStateMinFee().to_double();
        }
        ++i;
    }

    // sufficient max fee in start transaction, assume logical structure has been tested
    const csdb::Transaction& prim_new_state = pack.transactions().front();
    if (!SmartContracts::is_new_state(prim_new_state)) {
        // never should happens
        return Reject::Reason::MalformedTransaction;
    }
    csdb::Transaction start = SmartContracts::get_transaction(bc, prim_new_state);
    if (!start.is_valid()) {
        csdebug() << kLogPrefix << "incorrect start transaction is referred by new state, prevalidation failed";
        return Reject::Reason::MalformedTransaction;
    }
    const double avail_fee = start.max_fee().to_double() - start.counted_fee().to_double();
    if (avail_fee - total_min_fee < std::numeric_limits<double>::epsilon()) {
        csdebug() << kLogPrefix << "insufficient max fee in start transaction, prevalidation failed";
        return Reject::Reason::InsufficientMaxFee;
    }

    // test initer actual balance
    //csdb::Address initer_abs_addr = bc.getAddressByType(start.source(), BlockChain::AddressType::PublicKey);
    //BlockChain::WalletData wallet;
    //if (!bc.findWalletData(initer_abs_addr, wallet)) {
    //    csdebug() << kLogPrefix << "incorrect source wallet in start transaction, prevalidation failed";
    //    return false;
    //}
    //if (wallet.balance_.to_double() - total_min_fee < std::numeric_limits<double>::epsilon()) {
    //    csdebug() << kLogPrefix << "insufficient initer balance to pay min fee, prevalidation failed";
    //    return false;
    //}

    // all tests passed
    return Reject::Reason::None;
}

bool SmartContracts::test_executor_availability() {
    if (!executor_ready) {
        // ask user to restart executor every 2 seconds
        if (!wait_until_executor(2, 15)) { //15 times by 2 seconds
            cserror() << kLogPrefix << "cannot connect to executor, further blockchain reading is impossible, interrupt reading";
            if (pnode->isStopRequested()) {
                cslog() << kLogPrefix << "node is requested to stop, cancel wait to executor";
            }
            return false;
        }
        executor_ready = true;
        // update all contracts metadata, missed while executor was unavailable
        for (const auto& exe_item : exe_queue) {
            if (exe_item.status == SmartContractStatus::Running || exe_item.status == SmartContractStatus::Finished) {
                if (!is_metadata_actual(exe_item.abs_addr)) {
                    auto maybe_deploy = find_deploy_info(exe_item.abs_addr);
                    if (maybe_deploy.has_value()) {
                        auto it_state = known_contracts.find(exe_item.abs_addr);
                        if (it_state != known_contracts.end()) {
                            if (!update_metadata(maybe_deploy.value(), it_state->second, true /*skip_log*/)) {
                                if (!executor_ready) {
                                    // the problem has got back
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return executor_ready;
}

CallsQueueScheduler& SmartContracts::getScheduler() {
    return scheduler;
}

csdb::Transaction SmartContracts::get_actual_state(const csdb::Transaction& hashed_state, bool reading_db) {
    if (!is_new_state(hashed_state)) {
        return csdb::Transaction{};
    }
    using namespace trx_uf;
    if (hashed_state.user_field(new_state::Value).is_valid()) {
        return hashed_state;
    }

    csdb::UserField fld = hashed_state.user_field(new_state::Hash);
    if (!fld.is_valid()) {
        cserror() << kLogPrefix << "hashed_state contains neither state nor hash";
        return csdb::Transaction{};
    }

    std::string hash_string = fld.value<std::string>();
    cs::Hash hash;
    csdb::Transaction tr_state(0, hashed_state.source(), hashed_state.target(), hashed_state.currency(),
        hashed_state.amount(), hashed_state.max_fee(), hashed_state.counted_fee(), hashed_state.signature());

    for (const auto id : hashed_state.user_field_ids()) {
        if (id == new_state::Value) {
            continue;
        }
        if (id == new_state::Hash) {
            continue;
        }
        tr_state.add_user_field(id, hashed_state.user_field(id));
    }
    if (hash_string.size() != hash.size()) {
        cserror() << kLogPrefix << "hashed_state contains incompatible hash, use empty new_state";
    }
    else {
        std::copy(hash_string.cbegin(), hash_string.cend(), hash.begin());
        if (hash != cs::Zero::hash) {
            fld = hashed_state.user_field(new_state::RefStart);
            if (!fld.is_valid()) {
                cserror() << kLogPrefix << "hashed_state does not refer to start transaction, use empty new_state";
            }
            else {
                SmartContractRef ref_start(fld);
                csdb::Address req_abs_addr = absolute_address(hashed_state.target());
                // test last state in cache
                if (in_known_contracts(req_abs_addr)) {
                    const StateItem& item = known_contracts[req_abs_addr];
                    if (item.ref_execute == ref_start) {
                        cs::Hash current_hash = cscrypto::calculateHash((cs::Byte*)item.state.data(), item.state.size());
                        if (current_hash == hash) {
                            tr_state.add_user_field(new_state::Value, item.state);
                        }
                        else {
                            cswarning() << kLogPrefix << "incorrect " << ref_start << " state in cache, request from other nodes";
                            // request correct state in network and return empty new_state transaction as "no valid state available"
                            if (!reading_db) {
                                net_request_contract_state(req_abs_addr);
                            }
                            tr_state.add_user_field(new_state::Value, std::string{});
                        }
                    }
                }
                // execute contract to get last state if state is not found in cache
                if (tr_state.user_field_ids().count(new_state::Value) == 0) {
                    csdb::Transaction tr_start = get_transaction(ref_start, req_abs_addr);
                    if (!tr_start.is_valid()) {
                        cserror() << kLogPrefix << "get start transaction failed, use empty new_state";
                    }
                    else {
                        // test it is explicit "primary" call, otherwise request state in network
                        csdb::Address primary_abs_addr = absolute_address(tr_start.target());
                        if (req_abs_addr != primary_abs_addr) {
                            if (!reading_db) {
                                net_request_contract_state(req_abs_addr);
                            }
                        }
                        else {
                            SmartExecutionData exe_data;
                            exe_data.contract_ref = ref_start;
                            exe_data.abs_addr = req_abs_addr;
                            exe_data.executor_fee = csdb::Amount(tr_start.max_fee().to_double());
                            if (!SmartContracts::is_deploy(tr_start)) {
                                if (in_known_contracts(req_abs_addr)) {
                                    const StateItem& item = known_contracts[req_abs_addr];
                                    exe_data.explicit_last_state = item.state;
                                }
                            }
                            while (!execute(exe_data, true /*validationMode*/)) {
                                // execution error, test if executor is still available
                                if (!executor_ready) {
                                    // ask user to restart executor every 2 seconds
                                    if (!wait_until_executor(2)) {
                                        cserror() << kLogPrefix << "cannot connect to executor, further blockchain reading is impossible, interrupt reading";
                                        if (pnode->isStopRequested()) {
                                            cslog() << kLogPrefix << "node is requested to stop, cancel wait to executor";
                                        }
                                        return csdb::Transaction{};
                                    }
                                }
                                else {
                                    if (exe_data.error.empty()) {
                                        exe_data.error = "contract execution failed";
                                    }
                                    cserror() << kLogPrefix << "failed to get updated state of " << ref_start << ": " << exe_data.error;
                                    break;
                                }
                            }
                            if (!exe_data.result.smartsRes.empty()) {
                                const auto& head = exe_data.result.smartsRes.front();
                                if (head.response.code == 0) {
                                    // required contract address may differ from "primary" executed contract:

                                    if (head.states.count(req_abs_addr) == 0) {
                                        if (exe_data.result.response.code == error::TimeExpired) {
                                            cslog() << kLogPrefix << "timeout while executing contract, new state is not set";
                                        }
                                        else {
                                            cslog() << kLogPrefix << "contract new state is not set in execution result";
                                        }
                                    }
                                    else {
                                        const auto& state = head.states.at(req_abs_addr);
                                        // test actual hash
                                        if (!state.empty()) {
                                            cs::Hash actual_hash = cscrypto::calculateHash((cs::Byte*)state.data(), state.size());
                                            if (actual_hash == hash) {
                                                tr_state.add_user_field(new_state::Value, state);
                                                csdetails() << kLogPrefix << to_base58(req_abs_addr) << " state after " << ref_start
                                                    << " has updated, stored hash is OK, new size is " << state.size();
                                            }
                                            else {
                                                csdebug() << kLogPrefix << to_base58(req_abs_addr) << " state after " << ref_start
                                                    << " has updated, stored hash is WRONG: " << cs::Utils::byteStreamToHex(hash.data(), hash.size())
                                                    << " (expected " << cs::Utils::byteStreamToHex(actual_hash.data(), actual_hash.size())
                                                    << "), new size is " << state.size();
                                                if (!reading_db) {
                                                    net_request_contract_state(req_abs_addr);
                                                }
                                                else {
                                                    // update cache to current state and re-execute all future calls to this contract
                                                    if (in_known_contracts(req_abs_addr)) {
                                                        StateItem& item = known_contracts[req_abs_addr];
                                                        if (!item.ref_cache.is_valid() || item.ref_cache > ref_start) {
                                                            if (dbcache_update(req_abs_addr, ref_start, state, true /*force_update*/)) {
                                                                csdebug() << kLogPrefix << to_base58(req_abs_addr) << " state is replaced, all future calls will be re-executed";
                                                                item.state = state;
                                                                item.ref_cache = ref_start;
                                                            }
                                                            else {
                                                                cswarning() << kLogPrefix << "failed to replace " << to_base58(req_abs_addr) << " state in cache";
                                                            }
                                                        }
                                                    }
                                                    tr_state.add_user_field(new_state::Value, state);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // set empty new state in case of any problem
    if (tr_state.user_field_ids().count(new_state::Value) == 0) {
        tr_state.add_user_field(new_state::Value, std::string{});
    }
    return tr_state;
}

/*public*/
void SmartContracts::on_store_block(const csdb::Pool& block) {
    cs::RoundNumber cur_round = Conveyer::instance().currentRoundNumber();
    cs::Sequence last_block = bc.getLastSeq();

    cs::Lock lock(public_access_lock);
    bool should_stop = false;
    if (cur_round > last_block && cur_round - last_block > 1) {
        on_next_block_impl(block, true, &should_stop);
    }
    else {
        on_next_block_impl(block, false, &should_stop);
    }
}

/*public*/
void SmartContracts::on_read_block(const csdb::Pool& block, bool* should_stop) {
    cs::Lock lock(public_access_lock);
    on_next_block_impl(block, true, should_stop);

    if (block.sequence() == max_read_sequence && max_read_sequence > 0) {
        // validate contract states
        for (const auto& item : known_contracts) {
            const StateItem& val = item.second;
            if (val.state.empty()) {
                csdetails() << kLogPrefix << "completely unsuccessful " << val.ref_deploy << " found, neither deployed, nor executed";
            }
            if (!val.ref_deploy.is_valid()) {
                csdetails() << kLogPrefix << "unsuccessfully deployed contract found";
            }
        }
        csdebug() << kLogPrefix << "finish reading DB, " << WithDelimiters(known_contracts.size()) << " contracts were loaded";
    }
}

/*public*/
void SmartContracts::on_remove_block(const csdb::Pool& block) {
    if (!block.is_valid()) {
        return;
    }
    if (block.transactions_count() == 0) {
        return;
    }

    cs::Lock lock(public_access_lock);

    csdebug() << kLogPrefix << "block " << WithDelimiters(block.sequence()) << " is removed, rollback contracts";
    // rollback in reverse order:
    for (auto itr_t = block.transactions().crbegin(); itr_t != block.transactions().crend(); ++itr_t) {
        const auto& t = *itr_t;
        if (is_new_state(t)) {
            csdb::Address abs_addr = absolute_address(t.target());
            csdb::UserField fld = t.user_field(trx_uf::new_state::RefStart);
            if (fld.is_valid()) {
                SmartContractRef ref(fld);
                // put RUNNING item to exe_queue
                auto starter = get_transaction(ref, abs_addr);
                if (starter.is_valid()) {
                    auto it_queue = std::find_if(exe_queue.begin(), exe_queue.end(),
                        [&](const QueueItem& qi) { return qi.seq_enqueue == ref.sequence && qi.abs_addr == abs_addr; });
                    if (it_queue == exe_queue.end()) {
                        // add restored queue item
                        it_queue = exe_queue.emplace(exe_queue.cend(), QueueItem(ref, abs_addr, starter));
                        update_status(*it_queue, ref.sequence, SmartContractStatus::Running, true /*skip_log*/);
                        csdebug() << kLogPrefix << "last state of " << to_base58(abs_addr) << " is removed, restore queue item " << ref;
                    }
                    else {
                        // insert execution into existing queue item, due to reverse order, must insert into the beginning
                        it_queue->add(ref, starter, false /*at_end*/);
                        csdebug() << kLogPrefix << "last state of " << to_base58(abs_addr) << " is removed, add " << ref << " to existing queue item";
                    }
                    csdebug() << kLogPrefix << "last state of " << to_base58(abs_addr) << " is removed, restore previous state";
                }
                else {
                    // in fact, unreachable: cannot load start transaction by ref
                    cserror() << kLogPrefix << "cannot load block with start transaction " << ref << ", it is highly recommended to restart node";
                }

                // restore previous contract state
                if (in_known_contracts(abs_addr)) {
                    StateItem& item = known_contracts[abs_addr];
                    item.state.clear();

                    std::list<cs::Sequence> all_contract_blocks;
                    auto prev_seq = bc.getPreviousPoolSeq(abs_addr, t.id().pool_seq());
                    while (prev_seq != kWrongSequence) {
                        all_contract_blocks.insert(all_contract_blocks.cbegin(), prev_seq);
                        const auto tmp = bc.getPreviousPoolSeq(abs_addr, prev_seq);
                        if (tmp == 0 || tmp == prev_seq) {
                            break;
                        }
                        prev_seq = tmp;
                    }

                    // go through all blocks and re-execute contract
                    std::string executed_state;
                    csdb::Transaction executed_transaction;
                    SmartContractRef executed_ref;
                    if (!all_contract_blocks.empty()) {
                        bool request_state_required = false;
                        for (const auto seq : all_contract_blocks) {
                            const csdb::Pool b = bc.loadBlock(seq);
                            for (const auto& tt : b.transactions()) {
                                if (absolute_address(tt.target()) == abs_addr) {
                                    if (is_new_state(tt)) {
                                        // store new state in cache
                                        csdb::UserField fld_value = tt.user_field(trx_uf::new_state::Value);
                                        if (fld_value.is_valid()) {
                                            std::string tmp = fld_value.value<std::string>();
                                            if (!tmp.empty()) {
                                                item.state = tmp;
                                            }
                                        }
                                        else {
                                            if (!executed_state.empty()) {
                                                csdb::UserField fld_hash = tt.user_field(trx_uf::new_state::Hash);
                                                if (fld_hash.is_valid()) {
                                                    std::string hash_string = fld_hash.value<std::string>();
                                                    cs::Hash stored_hash;
                                                    if (hash_string.size() == stored_hash.size()) {
                                                        std::copy(hash_string.cbegin(), hash_string.cend(), stored_hash.begin());
                                                        cs::Hash executed_hash = cscrypto::calculateHash((cs::Byte*)executed_state.data(), executed_state.size());
                                                        if (executed_hash == stored_hash) {
                                                            item.state = executed_state;
                                                            item.execute = executed_transaction;
                                                            item.ref_execute = executed_ref;
                                                        }
                                                        else if (stored_hash != Zero::hash) {
                                                            cslog() << kLogPrefix << "result state does not match stored non-zero hash, re-execution failed";
                                                            request_state_required = true;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    else {
                                        // re-execute contract
                                        if (executed_state.empty() && !is_deploy(tt)) {
                                            cslog() << kLogPrefix << "unable call to contract not having its valid state, re-execution failed";
                                            request_state_required = true;
                                        }
                                        else {
                                            SmartExecutionData exe_data;
                                            exe_data.contract_ref.sequence = tt.id().pool_seq();
                                            exe_data.contract_ref.transaction = tt.id().index();
                                            exe_data.abs_addr = abs_addr;
                                            exe_data.executor_fee = csdb::Amount(tt.max_fee().to_double());
                                            exe_data.explicit_last_state = item.state;
                                            while (!execute(exe_data, true /*validationMode*/)) {
                                                // execution error, test if executor is still available
                                                if (!executor_ready) {
                                                    // ask user to restart executor every 2 seconds
                                                    if (!wait_until_executor(2, 15)) {
                                                        cserror() << kLogPrefix << "cannot connect to executor, contract re-excution is impossible";
                                                        if (pnode->isStopRequested()) {
                                                            cslog() << kLogPrefix << "node is requested to stop, cancel wait to executor";
                                                        }
                                                        request_state_required = true;
                                                        break;
                                                    }
                                                }
                                                else {
                                                    if (exe_data.error.empty()) {
                                                        exe_data.error = "contract execution failed";
                                                    }
                                                    cserror() << kLogPrefix << "failed to get updated state of " << exe_data.contract_ref << ": " << exe_data.error;
                                                    request_state_required = true;
                                                    break;
                                                }
                                            }
                                            if (!exe_data.result.smartsRes.empty()) {
                                                const auto& head = exe_data.result.smartsRes.front();
                                                if (head.response.code == 0) {
                                                    if (head.states.count(abs_addr) == 0) {
                                                        if (exe_data.result.response.code == error::TimeExpired) {
                                                            cslog() << kLogPrefix << "timeout while executing contract, new state is not set";
                                                        }
                                                        else {
                                                            cslog() << kLogPrefix << "contract new state is not set in execution result";
                                                        }
                                                    }
                                                    else {
                                                        executed_state = head.states.at(abs_addr);
                                                        executed_transaction = tt;
                                                        executed_ref.sequence = b.sequence();
                                                        executed_ref.transaction = tt.id().index();
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            if (request_state_required) {
                                item.state.clear();
                                // further re-execution is senseless
                                break;
                            }
                        }
                    }

                    if (item.state.empty()) {
                        net_request_contract_state(abs_addr);
                        csdebug() << kLogPrefix << "unable to restore new state of " << to_base58(abs_addr) << ", make a network request";
                    }
                    else {
                        if (dbcache_update(abs_addr, item.ref_execute, item.state, true)) {
                            item.ref_cache = item.ref_execute;
                            csdebug() << kLogPrefix << "last state of " << to_base58(abs_addr) << " is restored to " << item.ref_execute;
                        }
                        else {
                            cslog() << kLogPrefix << "failed to store in cache state of " << to_base58(abs_addr) << " restored to " << item.ref_execute;
                        }
                    }
                }
            }
        } // endif is_new_state(t)
        else if (is_executable(t)) {
            // erase execution from exe queue
            SmartContractRef ref;
            ref.sequence = t.id().pool_seq();
            ref.transaction = t.id().index();
            auto it_queue = find_in_queue(ref);
            if (it_queue != exe_queue.end()) {
                auto it_exe = find_in_queue_item(it_queue, ref);
                if (it_exe != it_queue->executions.end()) {
                    it_queue->executions.erase(it_exe);
                }
                if (it_queue->executions.empty()) {
                    exe_queue.erase(it_queue);
                }
            }
            else {
                csdebug() << kLogPrefix << "cannot find in queue deleted call " << ref << ", assume it was finished with timeout";
                rollback_contract_timeout(t);
            }
            // erase deploy transaction, erase contract at all
            if (is_deploy(t)) {
                csdb::Address abs_addr = absolute_address(t.target());
                csdebug() << kLogPrefix << "completely erase " << to_base58(abs_addr) << " as deploy " << ref << " in removed block";
                known_contracts.erase(abs_addr);
            }
        }
        else if (is_payable_target(t)) {
            SmartContractRef ref;
            ref.sequence = t.id().pool_seq();
            ref.transaction = t.id().index();
            auto it_queue = find_in_queue(ref);
            if (it_queue != exe_queue.end()) {
                /*emit*/ rollback_payable_invoke(t);
                auto it_exe = find_in_queue_item(it_queue, ref);
                if (it_exe != it_queue->executions.end()) {
                    it_queue->executions.erase(it_exe);
                }
                if (it_queue->executions.empty()) {
                    exe_queue.erase(it_queue);
                }
            }
            else {
                csdebug() << kLogPrefix << "cannot find in queue deleted call to payable() " << ref << ", assume it was finished with timeout";
                rollback_contract_timeout(t);
            }
        }
        else {
            csdb::Address abs_addr = absolute_address(t.source());
            if (in_known_contracts(abs_addr)) {
                // contract emitted transaction, look up corresponding new state
                for (auto tmp = itr_t + 1; tmp != block.transactions().crbegin(); ++tmp) {
                    if (SmartContracts::is_new_state(*tmp) && absolute_address(tmp->target()) == abs_addr) {
                        // proper new state found
                        const auto& last_new_state_transaction = *tmp;
                        csdb::UserField fld = last_new_state_transaction.user_field(trx_uf::new_state::RefStart);
                        if (fld.is_valid()) {
                            SmartContractRef ref(fld);
                            csdb::Transaction start_transaction = get_transaction(ref, abs_addr);
                            if (start_transaction.is_valid()) {
                                rollback_emitted_accepted(t, start_transaction);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
}

/*private*/
void SmartContracts::on_next_block_impl(const csdb::Pool& block, bool reading_db, bool* should_stop) {
    if (!should_stop) {
        cslog() << kLogPrefix << "stop is requested";
        return;
    }

    test_contracts_locks();

    const auto seq = block.sequence();
    for (auto& item : exe_queue) {
        // unconditionally cancel (and as well remove) over-MaxRoundsCancelContract items from queue
        if (item.seq_start > 0 && seq > item.seq_start && seq - item.seq_start >= Consensus::MaxRoundsCancelContract) {
            update_status(item, seq, SmartContractStatus::Canceled, reading_db);
            // goto next item in exe_queue
            continue;
        }

        if (item.status != SmartContractStatus::Running && item.status != SmartContractStatus::Finished) {
            // nothing to do with idle, waiting or canceled items in queue
            continue;
        }

        if (!reading_db) {
            // "near-timeout" and "out-of-fee" are actual only in real-time
            if (item.status == SmartContractStatus::Running) {
                // test near-timeout:
                if (seq > item.seq_start && seq - item.seq_start > Consensus::MaxRoundsExecuteContract) {
                    cslog() << kLogPrefix << FormatRef(item.seq_enqueue) << " is in queue over " << Consensus::MaxRoundsExecuteContract
                        << " blocks (from #" << item.seq_start << "), stop it";
                    if (item.is_executor) {
                        std::vector<SmartExecutionData> data_list;
                        for (const auto& execution : item.executions) {
                            SmartExecutionData& data = data_list.emplace_back();
                            data.contract_ref = execution.ref_start;
                            data.setError(error::TimeExpired, "contract execution timeout");
                        }
                        if (!data_list.empty()) {
                            on_execution_completed_impl(std::move(data_list));
                        }
                    }
                    else {
                        update_status(item, seq, SmartContractStatus::Finished, reading_db);
                    }
                    continue;
                }
                // test out-of-fee in every execution item and cancel all jobs if any out-of-fee occurs
                const auto add_fee = smart_round_fee(block); // 0 actually
                for (auto& execution : item.executions) {
                    execution.consumed_fee += add_fee;
                    if (execution.avail_fee < execution.consumed_fee) {
                        // cancel all item and break the loop
                        cslog() << kLogPrefix << '{' << execution.ref_start.sequence << '.' << execution.ref_start.transaction
                            << "} is out of fee, cancel the whole queue item";
                        if (item.is_executor) {
                            std::vector<SmartExecutionData> data_list;
                            for (const auto& e : item.executions) {
                                SmartExecutionData& data = data_list.emplace_back();
                                data.contract_ref = e.ref_start;
                                data.setError(error::OutOfFunds, "contract execution is out of funds");
                            }
                            if (!data_list.empty()) {
                                on_execution_completed_impl(std::move(data_list));
                            }
                        }
                        else {
                            update_status(item, seq, SmartContractStatus::Finished, reading_db);
                        }
                        break;
                    }
                }
            }  // if block for Running only contract
        }

    }  // for each exe_queue item

    // inspect transactions against smart contracts, raise special event on every item found:
    if (block.transactions_count() > 0) {
        size_t tr_idx = 0;
        for (const auto& tr : block.transactions()) {
            if (is_smart_contract(tr)) {
                if (is_new_state(tr)) {
                    if (!reading_db) {
                        csdebug() << kLogPrefix << "found new state " << FormatRef(block.sequence(), tr_idx);
                    }
                    csdb::UserField fld_contract_ref = tr.user_field(trx_uf::new_state::RefStart);
                    if (!fld_contract_ref.is_valid()) {
                        cserror() << kLogPrefix << "inconsistent new state "
                            << FormatRef(block.sequence(), tr_idx) << " does not contain reference to execute";
                    }
                    else {
                        SmartContractRef ref_start(fld_contract_ref);
                        // update state
                        const csdb::Address abs_addr = absolute_address(tr.target());
                        if (!update_contract_state(tr, reading_db)) {
                            if (!reading_db) {
                                csdebug() << kLogPrefix << to_base58(abs_addr) << " state is unchanged after " << ref_start;
                            }
                            if (pnode->isStopRequested()) {
                                *should_stop = true;
                                return;
                            }
                        }
                        remove_from_queue(ref_start, reading_db);
                    }
                    if (!reading_db) {
                        csdb::UserField fld_fee = tr.user_field(trx_uf::new_state::Fee);
                        if (fld_fee.is_valid()) {
                            FormatRef ref{ block.sequence(), tr_idx };
                            csdebug() << kLogPrefix << ref << " execution fee " << fld_fee.value<csdb::Amount>().to_double();
                            csdebug() << kLogPrefix << ref << " new state fee " << tr.counted_fee().to_double();
                        }
                    }
                }
                else {
                    if (!reading_db) {
                        if (this->is_deploy(tr)) {
                            csdebug() << kLogPrefix << "found deploy " << FormatRef(block.sequence(), uint32_t(tr_idx));
                        }
                        else {
                            csdebug() << kLogPrefix << "found execute " << FormatRef(block.sequence(), uint32_t(tr_idx));
                        }
                    }
                    enqueue(block, tr_idx, reading_db);
                }
            }
            else if (is_payable_target(tr)) {
                // replenish contract => execute payable method
                if (!reading_db) {
                    csdebug() << kLogPrefix << "found contract replenish " << FormatRef(block.sequence(), tr_idx);
                }
                emit signal_payable_invoke(tr);
                enqueue(block, tr_idx, reading_db);
            }
            else {
                // test if transaction is emitted by contract
                // such transactions ALWAYS follow the new_state, so corresponding new_state has already been processed and stored in known_contracts
                csdb::Address abs_addr = absolute_address(tr.source());
                const auto it = known_contracts.find(abs_addr);
                if (it != known_contracts.cend()) {
                    // is emitted by contract
                    const auto& contract_item = it->second;
                    csdb::Transaction starter = get_transaction(contract_item.ref_execute, abs_addr);
                    if (starter.is_valid()) {
                        if (!reading_db) {
                            csdetails() << kLogPrefix << "found emitted transaction in " << contract_item.ref_execute;
                        }
                        emit signal_emitted_accepted(tr, starter);
                    }
                    else {
                        cserror() << kLogPrefix << "failed to find starter transaction for contract emitted one";
                    }
                }
            }
            ++tr_idx;
        }
    }

    test_exe_queue(reading_db);
}

// return next element in queue
SmartContracts::queue_iterator SmartContracts::remove_from_queue(SmartContracts::queue_iterator it, bool skip_log) {
    if (it != exe_queue.cend()) {
        if (!skip_log) {
            csdebug() << kLogPrefix << "remove from queue completed item " << FormatRef(it->seq_enqueue);
            for (const auto item : it->executions) {
                csdebug() << "\t" << item.ref_start << "->" << print_executed_method(item.transaction);
            }
        }
        const cs::Sequence seq = bc.getLastSeq();
        const cs::Sequence seq_cancel = it->seq_start + Consensus::MaxRoundsCancelContract;
        if (!skip_log && seq >= it->seq_start + Consensus::MaxRoundsExecuteContract && seq < seq_cancel) {
            csdebug() << kLogPrefix << seq_cancel - seq << " round(s) remains until unconditional timeout";
        }
        // its too early to unlock contract(s), wait until states will updated
        // unlock only closed (after timeout) contracts
        if (it->status == SmartContractStatus::Canceled) {
            update_lock_status(*it, false, skip_log);
        }
        it = exe_queue.erase(it);

        if (!skip_log) {
            if (exe_queue.empty()) {
                csdebug() << kLogPrefix << "contract queue is empty, nothing to execute";
            }
            else {
                csdebug() << kLogPrefix << exe_queue.size() << " item(s) in queue";
            }
        }
    }

    return it;
}

void SmartContracts::remove_from_queue(const SmartContractRef& item, bool skip_log) {
    queue_iterator it = find_in_queue(item);
    if (it == exe_queue.end()) {
        return;
    }
    // find older items of the same contract and cancel them
    cs::Sequence seq = bc.getLastSeq();
    for (queue_iterator it_older = exe_queue.begin(); it_older != it; ++it_older) {
        if (it_older->abs_addr == it->abs_addr) {
            csdebug() << kLogPrefix << to_base58(it->abs_addr) << ' ' << FormatRef(it_older->seq_enqueue)
                << " is canceled by newer state after " << FormatRef(it->seq_enqueue) << " on " << WithDelimiters(seq);
            update_status(*it_older, seq, SmartContractStatus::Canceled, skip_log);
        }
    }
    // test current item status, set it to Running if is not yet
    if (it->status == SmartContractStatus::Waiting) {
        update_status(*it, seq, SmartContractStatus::Running, skip_log);
    }
    auto execution = find_in_queue_item(it, item);
    if (execution != it->executions.cend()) {
        if (!skip_log) {
            csdebug() << kLogPrefix << "remove from queue completed call "
                << execution->ref_start << "->" << print_executed_method(execution->transaction);
        }
        it->executions.erase(execution);
    }
    if (it->executions.empty()) {
        // unlock anyway if removed from queue only when executions are empty
        update_lock_status(it->abs_addr, false, skip_log);
        remove_from_queue(it, skip_log);
    }
}

bool SmartContracts::execute(SmartExecutionData& data, bool validationMode) {
    if (!data.result.smartsRes.empty()) {
        data.result.smartsRes.clear();
    }
    if (!exec_handler_ptr) {
        executor_ready = false;
        data.setError(error::ExecuteTransaction, "contract executor is unavailable");
        return false;
    }
    csdb::Transaction transaction = get_transaction(data.contract_ref, data.abs_addr);
    if (!transaction.is_valid()) {
        data.setError(error::InternalBug, "load starter transaction failed");
        return false;
    }
    if (validationMode) {
        csdetails() << kLogPrefix << "validating state after " << data.contract_ref << "::" << print_executed_method(transaction);
    }
    else {
        cslog() << kLogPrefix << "executing " << data.contract_ref << "::" << print_executed_method(transaction);
    }
    // using data.result.newState to pass previous (not yet cached) new state in case of multi-call to conrtract:
    std::vector<cs::Executor::ExecuteTransactionInfo> smarts;
    auto& info = smarts.emplace_back(cs::Executor::ExecuteTransactionInfo{});
    info.transaction = transaction;
    info.deploy = get_deploy_transaction(data.abs_addr);
    info.sequence = data.contract_ref.sequence;
    // data.executor_fee bring all available fee for future execution:
    info.feeLimit = data.executor_fee;
    data.executor_fee = csdb::Amount(0);
    info.convention = cs::Executor::MethodNameConvention::Default;
    if (!is_smart(transaction)) {
        // the most frequent fast test
        auto item = known_contracts.find(absolute_address(transaction.target()));
        if (item != known_contracts.end()) {
            StateItem& state = item->second;
            if (state.payable == PayableStatus::Implemented) {
                info.convention = cs::Executor::MethodNameConvention::PayableLegacy;
            }
            else if (state.payable == PayableStatus::ImplementedVer1) {
                info.convention = cs::Executor::MethodNameConvention::Payable;
            }
        }
    }
    std::optional<cs::Executor::ExecuteResult> maybe_result;
    if (validationMode) {
        // for now smarts always contains a one item:
        maybe_result = exec_handler_ptr->getExecutor().reexecuteContract(smarts.front(), data.explicit_last_state);
    }
    else {
        maybe_result = exec_handler_ptr->getExecutor().executeTransaction(smarts, data.explicit_last_state);
    }
    bool test_executor_ready = false;
    if (maybe_result.has_value()) {
        data.result = maybe_result.value();
        if (data.result.response.code == 0) {
            if (!data.result.smartsRes.empty()) {
                auto& result = data.result.smartsRes.front();
                if (result.response.code == 0) {
                    if (!validationMode) {
                        // calculate execution fee
                        csdb::Amount total_fee(0);
                        for (const auto r : data.result.smartsRes) {
                            // r.executionCost is in nanoseconds, as microseconds are required
                            total_fee += fee::getExecutionFee(r.executionCost / 1000);
                        }
#if defined(USE_SELF_MEASURED_FEE)
                        if (total_fee.to_double() < DBL_EPSILON) {
                            total_fee = fee::getExecutionFee(data.result.selfMeasuredCost);
                        }
#endif
                        if (total_fee > info.feeLimit) {
                            // out of fee detected
                            data.setError(error::OutOfFunds, "contract execution is out of funds");
                        }
                        else {
                            // update with actual value
                            data.executor_fee = total_fee;
                        }
                    }
                } else {
                    data.error = result.response.message;
                    if (data.error.empty()) {
                        data.error = "contract execution failed, new contract state is empty";
                    }
                    switch (result.response.code) {
                    case error::ExecutorIncompatible:
                    case error::NodeUnreachable:
                        // may or may not be connected, it is not ready
                        executor_ready = false;
                        //TODO: decide, maybe stop executor process
                        break;
                    }
                }
            }
            else {
                // smart result is empty!
                executor_ready = false;
                data.setError(error::ExecuteTransaction, "execution failed (check executor version), contract state is unchanged");
            }
        }
        else if (data.result.response.code == error::TimeExpired) {
            data.setError(error::ExecutorUnreachable, "execution timeout");
        }
        else if (data.result.response.code == error::ThriftException) {
            test_executor_ready = true;
            data.setError(error::ExecutorUnreachable, "execution failed, connection to executor has lost");
        }
        else {
            test_executor_ready = true;
            data.error = data.result.response.message;
            if (data.error.empty()) {
                data.setError(error::ExecuteTransaction, "execution failed, contract state is unchanged");
            }
        }
    }
    else {
        // the possible reason to return std::nullopt is the executor unconnected, otherwise there is an internal error (incorrect transaction and so on...)
        test_executor_ready = true;
        data.setError(error::ExecutorUnreachable, "execution failed, executor is unreachable");
    }
    // result
    if (!executor_ready) {
        return false;
    }
    else if (test_executor_ready) {
        executor_ready = exec_handler_ptr->getExecutor().isConnected();
        if (!executor_ready) {
            return false;
        }
    }
    return true;
}

// returns false if execution canceled, so caller may call to remove_from_queue()
bool SmartContracts::execute_async(const std::vector<ExecutionItem>& executions) {
    std::vector<SmartExecutionData> data_list;
    for (const auto& execution : executions) {
        SmartExecutionData& execution_data = data_list.emplace_back();
        const csdb::Transaction& start_tr = execution.transaction;
        execution_data.contract_ref = execution.ref_start;
        execution_data.abs_addr = absolute_address(start_tr.target());
        execution_data.executor_fee = execution.avail_fee;
        bool replenish_only = false;  // means indirect call to payable()
        if (!is_executable(start_tr)) {
            replenish_only = is_payable_target(start_tr);
            if (!replenish_only) {
                // it must be filtered before not to prevent other calls from execution
                cserror() << kLogPrefix << "unable execute neither deploy nor start/replenish transaction";
                return false;
            }
        }
        bool deploy = is_deploy(start_tr);
        csdebug() << kLogPrefix << "invoke api to remote executor to " << (deploy ? "deploy" : (!replenish_only ? "execute" : "replenish"))
            << " {" << execution.ref_start.sequence << '.' << execution.ref_start.transaction << '}';
    }

    if (data_list.empty()) {
        // in fact, it was tested before
        return false;
    }

    // create runnable object
    auto runnable = [this, data_list{std::move(data_list)}]() mutable {
        // actually, multi-execution list always refers to the same contract, so we need not to distinct different contracts last state
        std::string last_state;
        for (auto& data : data_list) {
            // use data.result.newStatef member to pass last contract's state in multi-call
            data.explicit_last_state = last_state;
            if (!execute(data, false /*validationMode*/) || data.result.smartsRes.empty()) {
                if (data.error.empty()) {
                    data.error = "failed to invoke contract";
                }
                // last_state is not updated
            }
            else {
                // remember last state for the next execution
                const auto& head = data.result.smartsRes.front();
                if (head.states.count(data.abs_addr) > 0) {
                    last_state = head.states.at(data.abs_addr);
                }
            }
        }
        return data_list;
    };

    // run async and watch result
    auto watcher = cs::Concurrent::run(cs::RunPolicy::CallQueuePolicy, runnable);
    cs::Connector::connect(&watcher->finished, this, &SmartContracts::on_execution_completed);

    return true;
}

void SmartContracts::on_execution_completed_impl(const std::vector<SmartExecutionData>& data_list) {
    using namespace trx_uf;
    if (data_list.empty()) {
        // actually is checked before
        return;
    }

    // any of data item "points" to the same queue item
    auto it = find_in_queue(data_list.front().contract_ref);
    if (it != exe_queue.end()) {
        if (it->status == SmartContractStatus::Finished || it->status == SmartContractStatus::Canceled) {
            // already finished (by timeout), no transaction required
            return;
        }
        update_status(*it, bc.getLastSeq(), SmartContractStatus::Finished, false /*skip_log*/);
    }
    else {
        return;
    }

    int64_t next_id = 0; // "lazy" initialization assumed

    for (const auto& data_item : data_list) {
        ExecutionItem* execution = nullptr;
        // create partial new_state transaction
        if (it != exe_queue.end()) {
            auto it_exe = find_in_queue_item(it, data_item.contract_ref);
            csdebug() << kLogPrefix << "execution of " << data_item.contract_ref << " has completed";
            if (it_exe != it->executions.end()) {
                execution = &(*it_exe);
            }
        }
        if (execution == nullptr) {
            // wtf data without execution item?
            continue;
        }

        execution->consumed_fee = data_item.executor_fee;
        cs::TransactionsPacket& packet = execution->result;
        if (packet.transactionsCount() > 0) {
            packet.clear();
        }

        if (next_id > 0) {
            ++next_id;
        }
        else {
            // 1st-time init
            auto starter = execution->transaction;
            if (starter.is_valid()) {
                next_id = next_inner_id(absolute_address(starter.target()));
            }
            else {
                next_id = 1;
            }
        }
        csdb::Transaction result = create_new_state(*execution, next_id);
        csdebug() << kLogPrefix << "set innerID = " << next_id << " in " << data_item.contract_ref << " new_state";

        // create partial failure if new_state is not created
        if(!result.is_valid()) {
            cserror() << kLogPrefix << "cannot find in queue just completed contract, so cannot create new_state";
            csdb::Transaction tmp = execution->transaction;
            if (!tmp.is_valid()) {
                return;
            }
            QueueItem fake(data_item.contract_ref, absolute_address(tmp.target()), tmp);
            if (!fake.executions.empty()) {
                result = create_new_state(fake.executions.front(), next_id); // use the same next_id again
            }
            else {  
                // wtf!
                cserror() << kLogPrefix << "failed to create new_state transaction, even empty";
            }
        }

        // finalize new_state transaction
        if (!data_item.error.empty() || data_item.result.smartsRes.empty()) {
            cslog() << kLogPrefix << "execution of " << data_item.contract_ref << " is failed: " << data_item.error << ", new state is empty";
            // result contains empty USRFLD[state::Value]
            result.add_user_field(new_state::Value, std::string{});
            // smartRes or result contains error code for retVal
            if (!data_item.result.smartsRes.empty()) {
                set_return_value(result, data_item.result.smartsRes.front().retValue);
            }
            else {
                set_return_value(result, data_item.result.response.code);
            }
            packet.addTransaction(result);
        }
        else {
            // could not get here if smartRes empty (see corresponding "if" above)
            const auto& execution_result = data_item.result.smartsRes.front();
            csdb::Address primary_abs_addr = absolute_address(result.target());
            if (execution_result.states.count(primary_abs_addr) == 0) {
                cswarning() << kLogPrefix << "primary " << data_item.contract_ref << " new state is empty";
                result.add_user_field(new_state::Value, std::string{});
                set_return_value(result, error::ContractError);
                packet.addTransaction(result);
            }
            else {
                const auto& primary_new_state = execution_result.states.at(primary_abs_addr);
                csdebug() << kLogPrefix << "execution of " << data_item.contract_ref << " is successful, new state size = " << primary_new_state.size();

                // put new state
                result.add_user_field(new_state::Value, primary_new_state);
                set_return_value(result, execution_result.retValue);
                packet.addTransaction(result);

                if (it != exe_queue.end()) {
                    // put primary emitted transactions
                    if (!execution_result.emittedTransactions.empty()) {
                        for (const auto& tr : execution_result.emittedTransactions) {
                            if (absolute_address(tr.source) != primary_abs_addr) {
                                // is not by primary contract emitted
                                continue;
                            }
                            csdetails() << kLogPrefix << "set innerID = " << next_id << " in " << data_item.contract_ref << " emitted transaction";
                            // auto inner id generating
                            csdb::Transaction tmp(
                                ++next_id,
                                tr.source,
                                tr.target,
                                result.currency(),
                                tr.amount,
                                result.max_fee(),
                                csdb::AmountCommission(0.0),
                                Zero::signature  // empty signature
                            );
                            packet.addTransaction(tmp);
                        }
                        csdebug() << kLogPrefix << "add " << execution_result.emittedTransactions.size()
                            << " emitted transaction(s) to " << data_item.contract_ref << " state";
                    }
                    else {
                        csdebug() << kLogPrefix << "no emitted transaction added to " << data_item.contract_ref;
                    }
                    // put subsequent new_states if any
                    if (execution_result.states.size() > 1) {
                        csdebug() << kLogPrefix << "add " << execution_result.states.size() - 1
                            << " subsequent new state(s) along with " << data_item.contract_ref << " state";
                        for (const auto& [addr, state] : execution_result.states) {
                            csdb::Address secondary_abs_addr = absolute_address(addr);
                            if (secondary_abs_addr == primary_abs_addr) {
                                continue;
                            }
                            auto it_call = find_in_queue_item(it, data_item.contract_ref);
                            if (it_call != it->executions.end()) {
                                int64_t secondary_next_id = next_inner_id(secondary_abs_addr);
                                csdb::Transaction t(
                                    ++secondary_next_id,
                                    addr,
                                    addr,
                                    result.currency(),
                                    csdb::Amount(0),
                                    result.max_fee(),
                                    csdb::AmountCommission(0.0),
                                    Zero::signature  // empty signature
                                );
                                csdebug() << kLogPrefix << "set innerID = " << secondary_next_id << " in "
                                    << data_item.contract_ref << " secondary contract new_state";
                                t.add_user_field(trx_uf::new_state::Value, state);
                                t.add_user_field(trx_uf::new_state::RefStart, data_item.contract_ref.to_user_field());
                                t.add_user_field(trx_uf::new_state::Fee, csdb::Amount(0));
                                set_return_value(t, ::general::Variant{});
                                packet.addTransaction(t);
                                if (!state.empty()) {
                                    // put subsequent contract emitted transactions
                                    for (const auto& tr : execution_result.emittedTransactions) {
                                        if (absolute_address(tr.source) != secondary_abs_addr) {
                                            // is not by the current contract emitted
                                            continue;
                                        }
                                        csdb::Transaction tmp2(
                                            ++secondary_next_id,
                                            tr.source,
                                            tr.target,
                                            result.currency(),
                                            tr.amount,
                                            result.max_fee(),
                                            csdb::AmountCommission(0.0),
                                            Zero::signature  // empty signature
                                        );
                                        packet.addTransaction(tmp2);
                                        csdebug() << kLogPrefix << "set innerID = " << secondary_next_id << " in "
                                            << data_item.contract_ref << " secondary emitted transaction";
                                    }
                                    csdebug() << kLogPrefix << "add " << execution_result.emittedTransactions.size()
                                        << " emitted transaction(s) to " << data_item.contract_ref << " state";
                                }
                            }
                        }
                    }
                }
            }
            // perform just created packet pre-validation
            if (packet.transactionsCount() > 0) {
                const auto reject = prevalidate_inner(packet);
                if (reject != Reject::Reason::None) {
                    csdb::Transaction tmp = packet.transactions().front().clone();
                    cswarning() << kLogPrefix << "packet result prevalidation failed, make " << data_item.contract_ref << " new state is empty";
                    tmp.add_user_field(new_state::Value, std::string{});
                    set_return_value(tmp, error::LogicViolation);
                    EventReport::sendRejectContractExecution(*pnode, data_item.contract_ref, reject);
                    packet.clear();
                    packet.addTransaction(tmp);
                }
            }
        }
    }

    // 'it' already has tested
    std::ostringstream os;
    for (const auto e : it->executions) {
        os << e.ref_start << ' ';
    }
    csdebug() << kLogPrefix << "starting " << os.str() << "consensus";
    if (!it->is_executor || !start_consensus(*it)) {
        if (!it->is_executor) {
            csdebug() << kLogPrefix << os.str() << "consensus need not to start";
        }
        else {
            cswarning() << kLogPrefix << os.str() << "consensus is not started, probably this node is not a confidant";
        }
    }
}

bool SmartContracts::start_consensus(QueueItem& item) {
    if (item.executions.empty()) {
        return false;
    }
    // create (multi-)packet:
    // new_state[0] + [ emitted_list[0] ] + [ susequent_state_list[0] ] + ... + new_state[n-1] + [ emitted_list[n-1] ] + [ subsequent_state_list[n-1] ]
    cs::TransactionsPacket integral_packet;

    // add all transactions to integral packet, perform final integral validation against total count and total content size
    size_t total_cnt = 0;
    size_t total_size = 0;
    for (auto& e : item.executions) {
        if (e.result.transactionsCount() > 0) {
            if (total_cnt >= Consensus::MaxContractResultTransactions || total_size >= Consensus::MaxPreliminaryBlockSize) {
                // some limit has been reached, make remaining executions failed
                csdb::Transaction tmp = e.result.transactions().front().clone();
                cswarning() << kLogPrefix << "integral packet result exceeds limitations, make " << e.ref_start << " new state is empty";
                tmp.add_user_field(trx_uf::new_state::Value, std::string{});
                set_return_value(tmp, error::LogicViolation);
                e.result.clear();
                e.result.addTransaction(tmp);
                integral_packet.addTransaction(tmp);
                EventReport::sendRejectContractExecution(*pnode, e.ref_start, Reject::Reason::LimitExceeded);
            }
            else {
                // add all result transactions to integral packet
                for (const auto& t : e.result.transactions()) {
                    integral_packet.addTransaction(t);
                    ++total_cnt;
                    total_size += t.to_byte_stream().size();
                }
            }
        }
    }

    // if re-run consensus
    uint8_t run_counter = 0;
    if (item.pconsensus) {
        run_counter = item.pconsensus->runCounter() + 1;
    }
    item.pconsensus = std::make_unique<SmartConsensus>();

    csdebug() << kLogPrefix << "start consensus Smart round [" << item.seq_start << '.' << static_cast<int>(run_counter) << ']';
    // inform slots if any, packet does not contain smart consensus' data!
    emit signal_smart_executed(integral_packet);

    const auto res = item.pconsensus->initSmartRound(integral_packet, run_counter, this->pnode, this);
    if (!res) {
        item.pconsensus.reset();
    }
    return res;
}

uint64_t SmartContracts::next_inner_id(const csdb::Address& addr) const {
    csdb::Address abs_addr = SmartContracts::absolute_address(addr);
    
    // lookup in blockchain
    BlockChain::WalletData wallData{};
    BlockChain::WalletId wallId{};
    uint64_t id = 1;
    if (bc.findWalletData(abs_addr, wallData, wallId)) {
        if (!wallData.trxTail_.empty()) {
            id = wallData.trxTail_.getLastTransactionId() + 1;
        }
    }
    //csdebug() << kLogPrefix << "next innerID " << id << " (from storage)";
    return id;
}

// private method, inner struct
double SmartContracts::ExecutionItem::calc_max_fee() const {
    if (transaction.is_valid()) {
        // clarify when transaction available, normally we always here
        return transaction.max_fee().to_double() - transaction.counted_fee().to_double();
    }
    return avail_fee.to_double(); // "by default" value
}

csdb::Transaction SmartContracts::create_new_state(const ExecutionItem& item, int64_t new_id) {
    csdb::Transaction src = item.transaction;
    if (!src.is_valid()) {
        return csdb::Transaction{};
    }
    csdb::Transaction result(new_id,        
                             src.target(),      // contract's address
                             src.target(),      // contract's address
                             src.currency(),    // source value
                             0,                 // amount
                             csdb::AmountCommission(/*item.calc_max_fee()*/(uint16_t)0), 
                             csdb::AmountCommission(item.new_state_fee.to_double()),
                             Zero::signature  // empty signature
    );
    // USRFLD1 - ref to start trx
    result.add_user_field(trx_uf::new_state::RefStart, item.ref_start.to_user_field());
    // USRFLD2 - total fee
    result.add_user_field(trx_uf::new_state::Fee, item.consumed_fee);

    // clarify counted fee
    csdb::AmountCommission stored_fee = result.counted_fee();
    if (stored_fee.get_raw() != cs::fee::getFee(result).get_raw()) {
        csdebug() << kLogPrefix << "state transaction fee is updated from " << stored_fee.to_double() << " to " << result.counted_fee().to_double();
    }

    return result;
}

// get & handle rejected transactions
// the aim is
//  - to perform consensus on successful and rejected executions again
//  - all executions following the rejected one are also rejected

/*public*/
void SmartContracts::on_reject(const std::vector<Node::RefExecution>& reject_list) {

    if (reject_list.empty()) {
        return;
    }

    cs::Lock lock(public_access_lock);

    // handle failed calls
    csdebug() << kLogPrefix << "get reject contract(s) signal";
    if (reject_list.empty()) {
        csdebug() << kLogPrefix << "rejected contract list is empty";
    }
    else {
        csdebug() << kLogPrefix << "" << reject_list.size() << " contract(s) are rejected";

        // group reject_list by block sequence
        std::map< cs::Sequence, std::list<uint32_t> > grouped_failed;
        for (const auto& item : reject_list) {
            grouped_failed[item.first].emplace_back(item.second);
        }

        for (auto& [sequence, executions] : grouped_failed) {
            if (executions.empty()) {
                // actually impossible
                continue;
            }
            for (auto n : executions) {
                csdebug() << kLogPrefix << FormatRef(sequence, n) << " is rejected";
            }

            for (auto it_queue = exe_queue.begin(); it_queue != exe_queue.end(); ) {
                if (it_queue->seq_enqueue == sequence) {
                    if (it_queue->is_rejected) {
                        // has already done before
                        break;
                    }
                    // remove outdated executions (duplicated transaction is rejected)
                    size_t cnt_ignored = executions.size();
                    const auto it_state = known_contracts.find(it_queue->abs_addr);
                    if (it_state != known_contracts.cend()) {
                        const SmartContractRef& last_success_exe = it_state->second.ref_execute;
                        if (last_success_exe.sequence == sequence) {
                            executions.remove_if([=](uint32_t n) { return n <= last_success_exe.transaction; });
                        }
                    }
                    cnt_ignored -= executions.size();
                    if (cnt_ignored > 0) {
                        csdebug() << kLogPrefix << cnt_ignored << " rejection(s) is/are not confirmed";
                        if (executions.empty()) {
                            // continue to next sequence
                            break;
                        }
                        for (auto n : executions) {
                            csdebug() << kLogPrefix << FormatRef(sequence, n) << " is confirmed to reject";
                        }
                    }

                    for (auto it_exe = it_queue->executions.begin(); it_exe != it_queue->executions.end(); ++it_exe) {
                        if (std::find(executions.cbegin(), executions.cend(), it_exe->ref_start.transaction) != executions.cend()) {
                            // found (maybe partially) rejected queue item
                            // it_exe here points to the first rejected call in multi-call
                            // replace this item result with empty new state
                            // also, replace with empty state all subsequent executions
                            it_queue->is_rejected = true;
                            const size_t cnt_rejected = it_queue->executions.cend() - it_exe;
                            const size_t cnt_accepted = it_exe - it_queue->executions.cbegin();
                            do {
                                // clear state of rejected execution
                                if (it_exe->result.transactionsCount() > 0) {
                                    csdb::Transaction empty_new_state = it_exe->result.transactions().front().clone();
                                    empty_new_state.add_user_field(trx_uf::new_state::Value, std::string{});
                                    set_return_value(empty_new_state, error::ConsensusRejected);
                                    it_exe->result.clear();
                                    it_exe->result.addTransaction(empty_new_state);
                                }
                                ++it_exe;
                            } while (it_exe != it_queue->executions.end());

                            csdebug() << kLogPrefix << FormatRef(sequence) << " is splitted onto "
                                << cnt_accepted << " accepted + " << cnt_rejected << " rejected calls";

                            break;
                        }
                    }

                    // finally, restart consensus on the queue item
                    if (it_queue->is_executor) {
                        if (!start_consensus(*it_queue)) {
                            cserror() << kLogPrefix << "failed to restart consensus on " << FormatRef(sequence);
                        }
                    }
                }

                if (it_queue->executions.empty()) {
                    // all jobs are rejected/restarted
                    it_queue = exe_queue.erase(it_queue);
                }
                if (it_queue == exe_queue.end()) {
                    break;
                }
                ++it_queue;
            }
        }
    }

    test_exe_queue(false /*skip_log*/);
}

/*public*/
void SmartContracts::on_update(const std::vector< csdb::Transaction >& states) {
    cs::Lock lock(public_access_lock);

    for (const auto& t : states) {
        // is called only in real time, not while read db
        update_contract_state(t, false);
    }
}

bool SmartContracts::update_contract_state(const csdb::Transaction& t, bool reading_db) {
    using namespace trx_uf;

    csdb::UserField fld = t.user_field(new_state::RefStart);
    if (!fld.is_valid()) {
        // new state transaction does not refer correctly to starter one
        return false;
    }
    SmartContractRef ref_start(fld);

    csdb::Address abs_addr = absolute_address(t.target());
    if (!abs_addr.is_valid()) {
        if (reading_db) {
            csdebug() << kLogPrefix << ref_start << " (error in blockchain) cannot find contract by address from new_state";
        }
        else {
            cserror() << kLogPrefix << ref_start << " failed to convert optimized address";
        }
        return false;
    }

    if (in_known_contracts(abs_addr)) {
        StateItem& item = known_contracts[abs_addr];
        if (item.ref_execute.is_valid()) {
            if (item.ref_execute == ref_start) {
                // as item.ref_execute is updated below in this method and only if dbcache_update() => true we can test it against duplicated update
                csdetails() << kLogPrefix << "state of " << item.ref_execute << " is already actual, ignore duplicated update";
                return true;
            }
            if (ref_start < item.ref_execute) {
                csdetails() << kLogPrefix << "state of " << item.ref_execute << " is newer than " << ref_start << ", ignore outdated update";
                return true;
            }
        }
    }

    csdb::Transaction t_state = get_actual_state(t, reading_db);
    if (!t_state.is_valid()) {
        cserror() << kLogPrefix << ref_start << " state is not updated, transaction does not contain it";
        return false;
    }
    fld = t_state.user_field(trx_uf::new_state::Value);
    std::string state_value = fld.value<std::string>();
    if (!state_value.empty()) {

        csdb::Transaction t_start = get_transaction(ref_start, abs_addr);
        if(!t_start.is_valid()) {
            if (reading_db) {
                csdebug() << kLogPrefix << ref_start << " (error in blockchain) cannot find starter transaction";
            }
            else {
                cswarning() << kLogPrefix << ref_start << " failed to read starter transaction";
            }
            return false;
        }
        bool deploy = SmartContracts::is_deploy(t_start);
        bool call = SmartContracts::is_executable(t_start) && !deploy;
        bool replenish = !deploy && !call;

        if (!reading_db) {
            cslog() << kLogPrefix << to_base58(abs_addr) << " state is updated by " << ref_start << ", new size is " << state_value.size();
        }

        // create or get contract state item
        StateItem& item = known_contracts[abs_addr];
        // update state value in cache if it is older then or equal to or unset
        constexpr bool force_update_contracts_cache = false;
        if (force_update_contracts_cache || !item.ref_cache.is_valid() || item.ref_cache < ref_start || item.ref_cache == ref_start) {
            if (!dbcache_update(abs_addr, ref_start, state_value, force_update_contracts_cache)) {
                if (reading_db) {
                    // update state in memory cache
                    std::string state_from_db;
                    SmartContractRef ref_from_db;
                    if (dbcache_read(abs_addr, ref_from_db /*output*/, state_from_db /*output*/)) {
                        if (!state_from_db.empty()) {

                            // do not update item.state with state_from_db, otherwise validation by execute is not possible!
                            
                            item.ref_cache = ref_from_db;
                            if (ref_from_db == ref_start) {
                                cs::Sequence seq = bc.getLastSeq();
                                csdebug() << kLogPrefix << to_base58(abs_addr) << " state after " << ref_start << " has reached cache in DB on "
                                    << WithDelimiters(seq);
                            }
                            else {
                                csdetails() << kLogPrefix << to_base58(abs_addr) << " state after " << ref_start << " is overridden by "
                                    << ref_from_db << " in DB";
                            }
                        }
                    }
                }
                else {
                    // unable to allow cache not to be updated
                    cserror() << kLogPrefix << "failed to update " << ref_start << " state in DB";
                    //this->executor_ready = false;
                    return false;
                }
            }
            else {
                item.ref_cache = ref_start;
                if (reading_db) {
                    csdetails() << kLogPrefix << to_base58(abs_addr) << "state after " << ref_start << " has updated cache in DB while reading or synchronizing blocks";
                }
                else {
                    csdetails() << kLogPrefix << ref_start << " updated state in DB";
                }
            }
        }

        // there is only one place to update state in "memory cache" and only after successful dbcache_update()!!!
        item.state = std::move(state_value);
        item.ref_state = SmartContractRef(t.id());
        // determine it is the result of whether deploy or execute
        if (!replenish) {
            // deploy is execute also
            if (deploy) {
                item.ref_deploy = ref_start;
                // deploy transaction has already stored
                if (!item.deploy.is_valid()) {
                    item.deploy = t_start.clone();
                }
            }
        }
        else {
            // new_state after replenish contract transaction
            if (!reading_db) {
                // handle replenish from on-the-air blocks
                if (!implements_payable(item.payable)) {
                    cserror() << kLogPrefix << "non-payable " << ref_start << " state is updated by replenish transaction";
                }
            }
        }
        item.ref_execute = ref_start;
        item.execute = t_start;

        // emits signal
        contract_state_updated(t_state);
    }
    else {
        // state_value is empty - erase replenish_contract item if exists
        if (!reading_db) {
            std::string error_message("execution is failed");
            fld = t_state.user_field(new_state::RetVal);
            if (fld.is_valid()) {
                ::general::Variant var = cs::Serializer::deserialize <::general::Variant>(fld.value<std::string>());
                if (var.__isset.v_byte) {
                    error_message = SmartContracts::get_error_message(var.v_byte);
                }
                else if (var.__isset.v_string) {
                    error_message = var.v_string;
                }
            }
            csdebug() << kLogPrefix << ref_start << " state is not updated, " << error_message;
        }
        return false;
    }
    return true;
}

bool SmartContracts::is_payable(const csdb::Address& abs_addr) {
    // the most frequent fast test
    auto item = known_contracts.find(abs_addr);
    if (item == known_contracts.end()) {
        // unknown contract
        return false;
    }

    StateItem& state = item->second;
    if (state.payable != PayableStatus::Unknown) {
        return implements_payable(state.payable);
    }

    // the first time test
    auto maybe_deploy = find_deploy_info(abs_addr);
    if (!maybe_deploy.has_value()) {
        // smth goes wrong, do not update contract state but return false result
        return false;
    }
    if (!update_metadata(maybe_deploy.value(), state, true /*skip_log*/)) {
        return false;
    }
    return implements_payable(state.payable);
}

bool SmartContracts::update_metadata(const api::SmartContractInvocation& contract, StateItem& state, bool skip_log) {
    if (!exec_handler_ptr) {
        return false;
    }
    executor::GetContractMethodsResult result;
    std::string error;
    auto& executor_instance = exec_handler_ptr->getExecutor();
    executor_instance.getContractMethods(result, contract.smartContractDeploy.byteCodeObjects);
    if (result.status.code != 0) {
        executor_ready = executor_instance.isConnected();
        if (!skip_log) {
            if (!result.status.message.empty()) {   
                cswarning() << kLogPrefix << result.status.message;
            }
            else {
                if (!executor_ready) {
                    cswarning() << kLogPrefix << "unable to connect to executor";
                }
                else {
                    cswarning() << kLogPrefix << "execution error " << int(result.status.code);
                }
            }
        }
        // remain payable status & using unknown for future calls
        return false;
    }

    state.payable = PayableStatus::Absent;
    // lookup payable(amount, currency) && annotations
    for (const auto& m : result.methods) {
        // payable status, continue tests if PayableStatus::Implemented, not PayableStatus::ImplementedVer1
        if (state.payable != PayableStatus::ImplementedVer1) {
            if (m.name == PayableName) {
                if (m.arguments.size() == 2) {
                    const auto& a0 = m.arguments[0];
                    if (m.returnType == TypeVoid && a0.type == TypeString) {
                        const auto& a1 = m.arguments[1];
                        if (a1.type == TypeString) {
                            state.payable = PayableStatus::Implemented;
                        }
                    }
                    else if (m.returnType == TypeString && a0.type == TypeBigDecimal) {
                        const auto& a1 = m.arguments[1];
                        if (a1.type == TypeByteArray) {
                            state.payable = PayableStatus::ImplementedVer1;
                        }
                    }
                }
            }
        }
        // uses
        if (!m.annotations.empty()) {
            for (const auto& a : m.annotations) {
                if (a.name == UsesContract) {
                    csdb::Address addr;
                    std::string method;
                    if (a.arguments.count(UsesContractAddr) > 0) {
                        std::vector<uint8_t> bytes;
                        if (DecodeBase58(a.arguments.at(UsesContractAddr), bytes)) {
                            addr = csdb::Address::from_public_key(bytes);
                            if (addr.is_valid()) {
                                if (a.arguments.count(UsesContractMethod) > 0) {
                                    method = a.arguments.at(UsesContractMethod);
                                }
                                auto& u = state.uses[m.name];
                                u[addr] = method;  // empty method name is allowed too
                            }
                        }
                    }
                }
            }
        }
    }

    return true;
}

void SmartContracts::add_uses_from(const csdb::Address& abs_addr, const std::string& method, std::vector<csdb::Address>& uses) {
    const auto it = known_contracts.find(abs_addr);
    if (it != known_contracts.cend()) {
        if (!is_metadata_actual(abs_addr)) {
            const csdb::Transaction& t = it->second.deploy;
            if (t.is_valid()) {
                auto maybe_invoke_info = get_smart_contract_impl(t);
                if (maybe_invoke_info.has_value()) {
                    // try update it->second.uses, make a call to ApiExec
                    if (!update_metadata(maybe_invoke_info.value(), it->second, true /*skip_log*/)) {
                        // metadata cannot be updated
                        //csdetails() << kLogPrefix << "failed to update " << to_base58(abs_addr) << " metadata";
                    }
                }
            }
        }

        for (const auto& [meth, subcalls] : it->second.uses) {
            if (meth != method) {
                continue;
            }
            for (const auto& [subaddr, submeth] : subcalls) {
                if (std::find(uses.cbegin(), uses.cend(), subaddr) != uses.cend()) {
                    continue;  // skip, already in uses
                }
                uses.emplace_back(subaddr);
                add_uses_from(subaddr, submeth, uses);
            }
        }
    }
}

std::string SmartContracts::print_executed_method(const csdb::Transaction& t) {
    if (!t.is_valid()) {
        return std::string();
    }
    if (is_executable(t)) {
        const auto maybe_invoke_info = get_smart_contract_impl(t);
        if (!maybe_invoke_info.has_value()) {
            return std::string();
        }
        const auto& invoke_info = maybe_invoke_info.value();
        if (invoke_info.method.empty()) {
            return std::string("constructor()");
        }
        std::ostringstream os;
        os << invoke_info.method << '(';
        size_t cnt_params = 0;
        for (const auto& p : invoke_info.params) {
            if (cnt_params > 0) {
                os << ',';
            }
            print(os, p);
            ++cnt_params;
        }
        os << ')';
        return os.str();
    }
    if (is_payable_target(t)) {
        // cuurently, the 2nd arg is user_field[1]
        std::string arg = t.user_field(trx_uf::ordinary::Text).value<std::string>();
        if (arg.empty()) {
            arg = "<empty>";
        }
        std::ostringstream os;
        os << PayableName << "(" << PayableArg0 << " = " << t.amount().to_double() << ", bundle = " << arg << ')';
        return os.str();
    }
    return std::string("???");
}

std::string SmartContracts::get_executed_method_name(const csdb::Transaction& t) {
    if (!t.is_valid()) {
        return std::string();
    }
    if (is_executable(t)) {
        const auto maybe_invoke_info = get_smart_contract_impl(t);
        if (!maybe_invoke_info.has_value()) {
            return std::string();
        }
        const auto& invoke_info = maybe_invoke_info.value();
        if (invoke_info.method.empty()) {
            return std::string("constructor");
        }
        return invoke_info.method;
    }
    if (is_payable_target(t)) {
        return PayableName;
    }
    return std::string();
}

// currently not used
csdb::Amount SmartContracts::smart_round_fee(const csdb::Pool& /*block*/) {
    csdb::Amount fee(0);
    //if (block.transactions_count() > 0) {
    //    for (const auto& t : block.transactions()) {
    //        fee += csdb::Amount(t.counted_fee().to_double());
    //    }
    //}
    return fee;
}

void SmartContracts::update_status(QueueItem& item, cs::RoundNumber r, SmartContractStatus status, bool skip_log) {
    if (item.status == status) {
        // prevent duplicated calls
        return;
    }
    item.status = status;

    switch (status) {
        case SmartContractStatus::Waiting:
            item.seq_enqueue = r;
            if (!skip_log) {
                csdebug() << kLogPrefix << FormatRef(item.seq_enqueue) << " is waiting from #" << r;
            }
            break;
        case SmartContractStatus::Running:
            item.seq_start = r;
            update_lock_status(item, true, skip_log);
            if (!skip_log) {
                csdebug() << kLogPrefix << FormatRef(item.seq_enqueue) << " is running from #" << r;
            }
            break;
        case SmartContractStatus::Finished:
            item.seq_finish = r;
            if (!skip_log) {
                csdebug() << kLogPrefix << FormatRef(item.seq_enqueue) << " is finished on #" << r;
            }
            break;
        case SmartContractStatus::Canceled:
            update_lock_status(item, false, skip_log);
            for (const auto& execution : item.executions) {
                const csdb::Transaction& t = execution.transaction;
                if (t.is_valid()) {
                    emit signal_contract_timeout(t);
                    std::string extra_info;
                    if (execution.ref_start.sequence != item.seq_start) {
                        std::ostringstream os;
                        os << " (started from " << WithDelimiters(item.seq_start) << ')';
                        extra_info = os.str();
                    }
                    csdetails() << kLogPrefix << to_base58(t.target()) << ' ' << execution.ref_start
                        << " is finished with timeout on " << WithDelimiters(r) << extra_info;
                }
                else {
                    csdebug() << kLogPrefix << "unknown contract " << execution.ref_start << " is finished with timeout on " << WithDelimiters(r);
                }
            }
            if (!skip_log) {
                csdebug() << kLogPrefix << FormatRef(item.seq_enqueue) << " is closed";
            }
            break;
        default:
            break;
    }
}

void SmartContracts::test_contracts_locks() {
    // lookup running items
    if (!exe_queue.empty()) {
        for (const auto& exe_item : exe_queue) {
            if (exe_item.status == SmartContractStatus::Running || exe_item.status == SmartContractStatus::Finished) {
                return;
            }
        }
    }
    // no running items, ensure no locked contracts
    if (!locked_contracts.empty()) {
        csdebug() << kLogPrefix << "find " << locked_contracts.size() << "  locked contract(s) which is not executed now, unlock";
        locked_contracts.clear();
    }
}

void SmartContracts::update_lock_status(const csdb::Address& abs_addr, bool value, bool skip_log) {
    if (value) {
        const auto result = locked_contracts.insert(abs_addr);
        if (!skip_log) {
            if (result.second) {
                csdebug() << kLogPrefix << "lock contract " << to_base58(abs_addr);
            }
            else {
                csdebug() << kLogPrefix << "ignore duplicated " << to_base58(abs_addr) << " lock";
            }
        }
    }
    else {
        auto it = locked_contracts.find(abs_addr);
        if (it != locked_contracts.end()) {
            locked_contracts.erase(it);
            if (!skip_log) {
                csdebug() << kLogPrefix << "unlock contract " << to_base58(abs_addr);
            }
        }
    }
}

/*static*/
bool SmartContracts::dbcache_update(const BlockChain& blockchain, const csdb::Address& abs_addr, const SmartContractRef& ref_start, const std::string& state, bool force_update) {
    if (!force_update) {
        // test if new data is actually newer than stored data
        cs::Bytes current_data;
        if (blockchain.getContractData(abs_addr, current_data)) {
            cs::IDataStream stream(current_data.data(), current_data.size());
            SmartContractRef current_ref;
            stream >> current_ref.sequence >> current_ref.transaction;
            if (current_ref.sequence > ref_start.sequence) {
                csdetails() << kLogPrefix << "contract state from " << current_ref << " was stored, ignore " << ref_start;
                return false;
            }
            if (current_ref.sequence == ref_start.sequence && current_ref.transaction >= ref_start.transaction) {
                if (current_ref.transaction == ref_start.transaction) {
                    csdetails() << kLogPrefix << "conract state from " << ref_start << " has already been stored, ignore duplication";
                }
                else {
                    csdetails() << kLogPrefix << "conract state from " << current_ref << " was stored, ignore " << ref_start;
                }
                return false;
            }
        }
    }

    cs::Bytes data;
    cs::ODataStream stream(data);
    stream << ref_start.sequence << ref_start.transaction << state;
    return blockchain.updateContractData(abs_addr, data);
}

/*static*/
bool SmartContracts::dbcache_read(const BlockChain& blockchain, const csdb::Address& abs_addr,
    SmartContractRef& ref_start /*output*/, std::string& state /*output*/) {

    cs::Bytes data;
    if (!blockchain.getContractData(abs_addr, data)) {
        return false;
    }
    cs::IDataStream stream(data.data(), data.size());
    stream >> ref_start.sequence >> ref_start.transaction >> state;
    // compatibility with obsolete format, possible the block hash has been read instead of contract state
    if (stream.isAvailable(sizeof(size_t)) && state.size() == cscrypto::kHashSize) {
        stream >> state;
    }
    bool ok = (stream.isValid() && !stream.isAvailable(1));
    if (!ok && !state.empty()) {
        state.clear();
    }
    return ok;

}

bool SmartContracts::dbcache_read(const csdb::Address& abs_addr, SmartContractRef& ref_start /*output*/, std::string& state /*output*/) {
    return SmartContracts::dbcache_read(bc, abs_addr, ref_start, state);
}

bool SmartContracts::dbcache_update(const csdb::Address& abs_addr, const SmartContractRef& ref_start, const std::string& state,
    bool force_update /*= false*/) {
    return SmartContracts::dbcache_update(bc, abs_addr, ref_start, state, force_update);
}

bool SmartContracts::wait_until_executor(unsigned int test_freq, unsigned int max_periods /*= std::numeric_limits<unsigned int>::max()*/) {
    if (!exec_handler_ptr) {
        cserror() << kLogPrefix << "executor is unavailable, cannot operate correctly";
        return false;
    }
    unsigned int counter = 0;
    while (!exec_handler_ptr->getExecutor().isConnected()) {
        if (pnode->isStopRequested()) {
            return false;
        }
        if (++counter >= max_periods) {
            return false;
        }
        cswarning() << kLogPrefix << "executor disconnected, wait until connection is restored to continue with blockchain";
        std::this_thread::sleep_for(std::chrono::seconds(test_freq));
    }
    executor_ready = true;
    return true;
}

void SmartContracts::net_request_contract_state(const csdb::Address& abs_addr) {
    pnode->sendStateRequest(abs_addr, cs::Conveyer::instance().confidants());
}

/*public*/
void SmartContracts::net_update_contract_state(const csdb::Address& contract_abs_addr, const cs::Bytes& contract_data) {
    cs::Lock lock(public_access_lock);

    cs::SmartContractRef ref;
    std::string state;
    cs::IDataStream stream(contract_data.data(), contract_data.size());
    stream >> ref.sequence >> ref.transaction >> state;

    if (stream.isValid() && !stream.isAvailable(1)) {

        if (in_known_contracts(contract_abs_addr)) {
            auto& item = known_contracts[contract_abs_addr];
            if (item.ref_execute > ref) {
                cswarning() << kLogPrefix << "ignore outdated " << cs::SmartContracts::to_base58(contract_abs_addr) << " state";
                return;
            }
            // test state hash before dbcache_update
            csdebug() << kLogPrefix << "test " << cs::SmartContracts::to_base58(contract_abs_addr) << " state received";
            bool state_found = false;
            bool state_test_passed = false;
            SmartContractRef ref_state;
            for (cs::Sequence i = ref.sequence; i < ref.sequence + Consensus::MaxRoundsCancelContract; ++i) {
                csdb::Pool b = bc.loadBlock(i);
                if (b.is_valid() && b.transactions_count() > 0) {
                    for (const auto& t : b.transactions()) {
                        if (is_new_state(t)) {
                            csdb::UserField fld = t.user_field(trx_uf::new_state::RefStart);
                            if (fld.is_valid()) {
                                if (SmartContractRef(fld) == ref) {
                                    // corresponding new state found, test it
                                    state_found = true;
                                    ref_state.sequence = t.id().pool_seq();
                                    ref_state.transaction = t.id().index();
                                    fld = t.user_field(trx_uf::new_state::Hash);
                                    if (fld.is_valid()) {
                                        std::string hash_bytes = fld.value<std::string>();
                                        cs::Hash hash;
                                        if (hash.size() == hash_bytes.size()) {
                                            std::copy(hash_bytes.cbegin(), hash_bytes.cend(), hash.begin());
                                            if (state.empty()) {
                                                state_test_passed = (hash == cs::Zero::hash);
                                            }
                                            else {
                                                state_test_passed = (hash == cscrypto::calculateHash((cs::Byte*)state.data(), state.size()));
                                            }
                                        }
                                    }
                                    break; // transactions cycle
                                }
                            }
                        }
                    }
                }
                if (state_found) {
                    break;
                }
            }
            if (state_found && !state_test_passed) {
                cswarning() << kLogPrefix << to_base58(contract_abs_addr) << " state received does not match hash value in " << ref_state;
                return;
            }
        }
        
        if (dbcache_update(contract_abs_addr, ref, state, false)) {
            if (in_known_contracts(contract_abs_addr)) {
                auto& item = known_contracts[contract_abs_addr];
                item.state = state;
                item.ref_cache = ref;
                item.ref_execute = ref;
                csdebug() << kLogPrefix << to_base58(contract_abs_addr) << " state has updated from net package with " << ref << " state value";
            }
            return;
        }
        else {
            cswarning() << kLogPrefix << "ignore outdated net package with " << cs::SmartContracts::to_base58(contract_abs_addr)
                << " state";
        }
    }
    else {
        cswarning() << kLogPrefix << "ignore incompatible net package with " << cs::SmartContracts::to_base58(contract_abs_addr)
            << " state";
    }
}

Reject::Reason SmartContracts::prevalidate_inner(const cs::TransactionsPacket& pack) {
    const auto reject = SmartContracts::prevalidate(bc, pack);
    if (reject != Reject::Reason::None) {
        return reject;
    }
    // detect contract to contract payments
    for (const auto& t : pack.transactions()) {
        if (!SmartContracts::is_new_state(t)) {
            if (in_known_contracts(t.source()) && in_known_contracts(t.target())) {
                return Reject::Reason::MalformedTransaction;
            }

        }
    }
    return Reject::Reason::None;
}

std::vector<cs::TransactionsPacket> SmartContracts::grepNewStatesPacks(const BlockChain& storage, const std::vector<csdb::Transaction>& trxs) {
    PacketsVector res;
    cs::TransactionsPacket pack;
    SmartContractRef currentRef;
    SmartContractRef newRef;
    csdb::Address zeroSource = csdb::Address::from_public_key(cs::Zero::key);
    csdb::Address currentSource = zeroSource;
    size_t counter = 0;
    for (auto& it : trxs) {
        ++counter;
        csdb::Address abs_addr = storage.getAddressByType(it.source(), BlockChain::AddressType::PublicKey);

        if (SmartContracts::is_new_state(it)) {

            csdb::UserField fld;
            fld = it.user_field(trx_uf::new_state::RefStart);
            if (fld.is_valid()) {
                SmartContractRef ref(fld);
                if (ref.is_valid()) {
                    newRef = ref;
                }
                else {
                    break;
                }
            }

            if (!currentRef.is_valid() && abs_addr != zeroSource) {
                currentRef = newRef;
                currentSource = abs_addr;
                pack.addTransaction(it);
                continue;
            }
            else {
                if (abs_addr == currentSource || newRef == currentRef) {
                    if (abs_addr != currentSource) {
                        currentSource = abs_addr;
                    }
                    pack.addTransaction(it);
                    continue;
                }
                else {
                    currentRef = newRef;
                    currentSource = abs_addr;
                    //pack.makeHash();
                    res.push_back(pack);
                    pack = TransactionsPacket();
                    pack.addTransaction(it);
                    continue;
                }
            }

        }
        if (abs_addr == currentSource && abs_addr != zeroSource) {
            pack.addTransaction(it);
            continue;
        }
        else {
            if (pack.transactionsCount() > 0) {
                //pack.makeHash();
                res.push_back(pack);
                pack = TransactionsPacket();
            }
            currentRef = SmartContractRef{};
            currentSource = zeroSource;
        }

    }

    if (pack.transactionsCount() > 0) {
        //pack.makeHash();
        res.push_back(pack);
    }
    return res;
}

}  // namespace cs

