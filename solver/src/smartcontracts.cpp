#include <smartcontracts.hpp>
#include <solvercontext.hpp>

#include <ContractExecutor.h>
#include <csdb/currency.hpp>
#include <csnode/datastream.hpp>
#include <lib/system/logger.hpp>

#include <memory>
#include <optional>
#include <sstream>

namespace cs {

csdb::UserField SmartContractRef::to_user_field() const {
  cs::Bytes data;
  cs::DataStream stream(data);
  stream << hash << sequence << transaction;
  return csdb::UserField(std::string(data.cbegin(), data.cend()));
}

void SmartContractRef::from_user_field(csdb::UserField fld) {
  std::string data = fld.value<std::string>();
  cs::DataStream stream(data.c_str(), data.size());
  stream >> hash >> sequence >> transaction;
  if (!stream.isValid() || stream.isAvailable(1)) {
    cserror() << "SmartCotractRef: read form malformed user field, abort!";
    hash = csdb::PoolHash{};
    sequence = std::numeric_limits<decltype(sequence)>().max();
    transaction = std::numeric_limits<decltype(transaction)>().max();
  }
}

/*explicit*/
SmartContracts::SmartContracts(BlockChain& blockchain, CallsQueueScheduler& calls_queue_scheduler)
: execution_allowed(true)
, force_execution(false)
, bc(blockchain)
, scheduler(calls_queue_scheduler) {
#if defined(DEBUG_SMARTS)
  execution_allowed = false;
#endif

  // signals subscription
  cs::Connector::connect(&bc.storeBlockEvent_, this, &SmartContracts::on_store_block);
  cs::Connector::connect(bc.getStorage().read_block_event(), this, &SmartContracts::on_read_block);
}

SmartContracts::~SmartContracts() = default;

void SmartContracts::init(const cs::PublicKey& id, csconnector::connector::ApiHandlerPtr api)
{
  papi = api;
  node_id = id;

  size_t cnt = known_contracts.size();

  // consolidate contract states addressed by public keys with by wallet ids
  auto pred = [](const auto& item) { return item.first.is_wallet_id(); };
  auto it = std::find_if(known_contracts.cbegin(), known_contracts.cend(), pred);
  while(it != known_contracts.cend()) {
    // non-absolute address item is always newer then absolute one:
    csdb::Address abs_addr = absolute_address(it->first);
    if(abs_addr.is_valid()) {
      const StateItem& opt_out = it->second;
      if(!opt_out.state.empty()) {
        StateItem& updated = known_contracts[abs_addr];
        if(opt_out.deploy.is_valid()) {
          if(updated.deploy.is_valid()) {
            cswarning() << name() << ": contract deploy is overwritten by subsequent deploy of the same contract";
          }
          updated.deploy = opt_out.deploy;
          updated.state = opt_out.state;
        }
        if(opt_out.execution.is_valid()) {
          updated.execution = opt_out.execution;
          updated.state = opt_out.state;
        }
      }
      else {
        cswarning() << name() << ": empty state stored in contracts states table";
      }
    }
    known_contracts.erase(it);
    it = std::find_if(known_contracts.begin(), known_contracts.end(), pred);
  }

  // validate contract states
  for(const auto& item : known_contracts) {
    const StateItem& val = item.second;
    if(val.state.empty()) {
      cswarning() << name() << ": completely unsuccessful contract found, neither deployed, nor executed";
    }
    if(!val.deploy.is_valid()) {
      cswarning() << name() << ": unsuccessfully deployed contract found";
    }
  }

  size_t new_cnt = known_contracts.size();
  cslog() << name() << ": " << new_cnt << " smart contract states loaded";
  if(cnt > new_cnt) {
    cslog() << name() << ": " << cnt - new_cnt << " smart contract states is optimizied";
  }
}

/*static*/
bool SmartContracts::is_smart_contract(const csdb::Transaction tr) {
  if (!tr.is_valid()) {
    return false;
  }
  // to contain smart contract trx must contain either FLD[0] (deploy, start) or FLD[-2] (new_state), both of type
  // "String":
  csdb::UserField f = tr.user_field(trx_uf::deploy::Code);
  if (!f.is_valid()) {
    f = tr.user_field(trx_uf::new_state::Value);
  }
  return f.is_valid() && f.type() == csdb::UserField::Type::String;
}

/*static*/
bool SmartContracts::is_executable(const csdb::Transaction tr) {
  return SmartContracts::is_smart_contract(tr) && !SmartContracts::is_new_state(tr);
}

/*static*/
bool SmartContracts::is_deploy(const csdb::Transaction tr) {
  if (!SmartContracts::is_executable(tr)) {
    return false;
  }

  using namespace cs::trx_uf;
  csdb::UserField uf = tr.user_field(deploy::Code);
  if (!uf.is_valid()) {
    return false;
  }

  const auto invoke = deserialize<api::SmartContractInvocation>(uf.value<std::string>());  // get_smart_contract(tr);
  // deploy ~ start but method in invoke info is empty
  return invoke.method.empty();
}

/*static*/
bool SmartContracts::is_start(const csdb::Transaction tr) {
  return SmartContracts::is_executable(tr) && !SmartContracts::is_deploy(tr);
}

/*static*/
bool SmartContracts::is_new_state(const csdb::Transaction tr)
{
  // must contain user field new_state::Value and new_state::RefStart
  using namespace cs::trx_uf;
  // test user_field[RefStart] helps filter out ancient smart contracts:
  return (tr.user_field(new_state::Value).type() == csdb::UserField::Type::String &&
    tr.user_field(new_state::RefStart).type() == csdb::UserField::Type::String);
}

/* static */
/* Assuming deployer.is_public_key() */
csdb::Address SmartContracts::get_valid_smart_address(const csdb::Address& deployer, const uint64_t trId,
                                                      const api::SmartContractDeploy& data) {
  static_assert(cscrypto::kHashSize == cscrypto::kPublicKeySize);

  std::vector<cscrypto::Byte> strToHash;
  std::string byteCode{};
  if (!data.byteCodeObjects.empty()) {
    for (auto& curr_byteCode : data.byteCodeObjects) {
      byteCode += curr_byteCode.byteCode;
    }
  }
  strToHash.reserve(cscrypto::kPublicKeySize + 6 + byteCode.size());

  const auto dPk = deployer.public_key();
  const auto idPtr = reinterpret_cast<const cscrypto::Byte*>(&trId);

  std::copy(dPk.begin(), dPk.end(), std::back_inserter(strToHash));
  std::copy(idPtr, idPtr + 6, std::back_inserter(strToHash));
  std::copy(byteCode.begin(), byteCode.end(), std::back_inserter(strToHash));

  cscrypto::Hash result;
  cscrypto::CalculateHash(result, strToHash.data(), strToHash.size());

  return csdb::Address::from_public_key(reinterpret_cast<char*>(result.data()));
}

/*static*/
csdb::Transaction SmartContracts::get_transaction(BlockChain& storage, const SmartContractRef& contract) {
  csdb::Pool block = storage.loadBlock(contract.sequence);
  if (!block.is_valid()) {
    return csdb::Transaction{};
  }
  if (contract.transaction >= block.transactions_count()) {
    return csdb::Transaction{};
  }
  return block.transactions().at(contract.transaction);
}

void SmartContracts::checkAllExecutions() {
  using Watcher = cs::FutureWatcherPtr<SmartExecutionData>;
  std::vector<std::list<Watcher>::iterator> iterators;

  for (auto iter = executions_.begin(); iter != executions_.end(); ++iter) {
    if ((*iter)->state() == cs::WatcherState::Compeleted) {
      iterators.push_back(iter);
    }
  }

  for (auto iter : iterators) {
    executions_.erase(iter);
  }
}

std::optional<api::SmartContractInvocation> SmartContracts::find_deploy_info(const csdb::Address abs_addr) const
{
  using namespace trx_uf;

  const auto item = known_contracts.find(abs_addr);
  if(item != known_contracts.cend()) {
    const StateItem& val = item->second;
    if(val.deploy.is_valid()) {
      csdb::Transaction tr_deploy = get_transaction(val.deploy);
      if(tr_deploy.is_valid()) {
        csdb::UserField fld = tr_deploy.user_field(deploy::Code);
        if(fld.is_valid()) {
          std::string data = fld.value<std::string>();
          if(!data.empty()) {
            return std::make_optional(std::move(deserialize<api::SmartContractInvocation>(std::move(data))));
          }
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<api::SmartContractInvocation> SmartContracts::get_smart_contract(const csdb::Transaction tr) {
    // currently calls to is_***() from this method are prohibited, infinite recursion is possible!
  using namespace trx_uf;

  bool is_replenish_contract = false;
  if( !is_smart_contract( tr ) ) {
    is_replenish_contract = is_payable_target( tr );
    if( !is_replenish_contract ) {
      return std::nullopt;
    }
  }

  const csdb::Address abs_addr = absolute_address(tr.target());

  // get info from private contracts table (faster), not from API
 
  if(is_new_state(tr) || is_replenish_contract) {
    auto maybe_contract = find_deploy_info(abs_addr);
    if(maybe_contract.has_value()) {
      return maybe_contract;
    }
  }
  // is executable (deploy or start):
  else {
    const csdb::UserField fld = tr.user_field(deploy::Code);  // start::Methods == deploy::Code, so does not matter what type of executable is
    if(fld.is_valid()) {
      std::string data = fld.value<std::string>();
      if(!data.empty()) {
        auto invoke = deserialize<api::SmartContractInvocation>(std::move(data));
        if(invoke.method.empty()) {
          // is deploy
          return std::make_optional(std::move(invoke));
        }
        else {
          // is start
          auto maybe_deploy = find_deploy_info(abs_addr);
          if(maybe_deploy.has_value()) {
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

bool SmartContracts::is_payable_target( const csdb::Transaction tr ) {
  csdb::Address abs_addr = absolute_address( tr.target() );
  if( !is_known_smart_contract( abs_addr ) ) {
    return false;
  }
  // may do blocking call to API::executor
  return is_payable( abs_addr );
}

void SmartContracts::clear_emitted_transactions(const csdb::Address abs_addr) {
  std::lock_guard<std::mutex> lock(mtx_emit_transaction);

  if(emitted_transactions.count(abs_addr) > 0) {
    emitted_transactions.erase(abs_addr);
  }
}

//  const csdb::PoolHash blk_hash, cs::Sequence blk_seq, size_t trx_idx, cs::RoundNumber round
void SmartContracts::enqueue(csdb::Pool block, size_t trx_idx) {
  if (trx_idx >= block.transactions_count()) {
    cserror() << name() << ": incorrect trx index in block to enqueue smart contract";
    return;
  }
  SmartContractRef new_item(block.hash().clone(), block.sequence(), trx_idx);
  if (!exe_queue.empty()) {
    auto it = find_in_queue(new_item);
    // test duplicated contract call
    if (it != exe_queue.cend()) {
      cserror() << name() << ": attempt to queue duplicated contract transaction, already queued on round #"
                << it->round_enqueue;
      return;
    }
  }

  // enqueue to end
  csdb::Transaction t = block.transaction(trx_idx);
  csdb::Address abs_addr = absolute_address(t.target());
  bool payable = false;
  if(is_deploy(t)) {
    // pre-register in known_contracts if payable
    auto maybe_invoke_info = get_smart_contract(t);
    if(maybe_invoke_info.has_value()) {
      const auto& invoke_info = maybe_invoke_info.value();
      StateItem& val = known_contracts[abs_addr];
      val.deploy = new_item;
      if(implements_payable(invoke_info)) {
        val.payable = PayableStatus::Implemented;
        payable = true;
      }
      else {
        val.payable = PayableStatus::Absent;
      }
    }
  }
  else {
    payable = is_payable(abs_addr);
  }

  cslog() << "  _____";
  cslog() << " /     \\";
  if(payable) {
    cslog() << "/   $   \\";
  }
  else {
    cslog() << "/   +   \\";
  }
  cslog() << "\\       /";
  cslog() << " \\_____/" << "  " << get_executed_method(new_item);

  exe_queue.emplace_back(QueueItem(new_item, abs_addr)).wait(new_item.sequence);
  test_exe_queue();
}

void SmartContracts::on_new_state(csdb::Pool block, size_t trx_idx) {
  if (!block.is_valid() || trx_idx >= block.transactions_count()) {
    cserror() << name() << ": incorrect new_state transaction specfied";
  }
  else {
    auto new_state = get_transaction(SmartContractRef{block.hash(), block.sequence(), trx_idx});
    if (!new_state.is_valid()) {
      cserror() << name() << ": get new_state transaction failed";
    }
    else {
      csdb::UserField fld_contract_ref = new_state.user_field(trx_uf::new_state::RefStart);
      if (!fld_contract_ref.is_valid()) {
        cserror() << name() << ": new_state transaction does not contain reference to contract";
      }
      else {
        SmartContractRef contract_ref(fld_contract_ref);
        // update state
        csdebug() << name() << ": updating contract state";
        update_contract_state(new_state);
        remove_from_queue(contract_ref);
      }
    }
  }

  test_exe_queue();
}

void SmartContracts::test_exe_queue() {
  // select next queue item
  while (!exe_queue.empty()) {
    auto next = exe_queue.begin();
    if (next->status == SmartContractStatus::Closed) {
      csdebug() << name() << ": finished contract still in queue, remove it";
      remove_from_queue(exe_queue.cbegin());
      continue;
    }
    if(next->status == SmartContractStatus::Running) {
      // some contract is already running
      csdebug() << name() << ": running contract is in queue";
      break;
    }
    if(next->status == SmartContractStatus::Finished) {
      // some contract is already running
      csdebug() << name() << ": finished contract is in queue";
      break;
    }
    csdebug() << name() << ": set running status to next contract in queue";
    clear_emitted_transactions(next->abs_addr); // insurance
    next->start( bc.getLastSequence() ); // use blockchain based round counting
    if (!invoke_execution(next->contract)) {
      remove_from_queue(next);
    }
  }

  if (exe_queue.empty()) {
    csdebug() << name() << ": contract queue is empty, nothing to execute";
  }
}

bool SmartContracts::is_running_smart_contract(csdb::Address addr) const {
  if (!exe_queue.empty()) {
    const auto it = find_in_queue(absolute_address(addr));
    if(it != exe_queue.cend()) {
      return it->status == SmartContractStatus::Running;
    }
  }
  return false;
}

bool SmartContracts::is_closed_smart_contract(csdb::Address addr) const
{
  if(!exe_queue.empty()) {
    const auto it = find_in_queue(absolute_address(addr));
    if(it != exe_queue.cend()) {
      return it->status == SmartContractStatus::Closed;
    }
  }
  return true;
}

bool SmartContracts::capture(csdb::Transaction tr)
{
  // test smart contract as source of transaction
  csdb::Address abs_addr = absolute_address(tr.source());
  if(known_contracts.find(abs_addr) != known_contracts.end()) {
    if(is_running_smart_contract(abs_addr)) {
      // expect calls from api when trxs received
      std::lock_guard<std::mutex> lock(mtx_emit_transaction);

      auto& vect = emitted_transactions[abs_addr];
      vect.push_back(tr);
      csdebug() << name() << ": smart contract emits transaction, add, total " << vect.size();
    }
    else {
      csdebug() << name() << ": smart contract is not allowed to emit transaction, drop it";
    }
    return true; // block from conveyer sync
  }

  // test smart contract as target of transaction (is it payable?)
  abs_addr = absolute_address(tr.target());
  if(is_known_smart_contract(abs_addr)) {
    double amount = tr.amount().to_double();
    // possible blocking call to executor for the first time:
    if(!is_payable(abs_addr)) {
      if(amount > std::numeric_limits<double>::epsilon()) {
        cslog() << name() << ": unable replenish balance of contract without payable() feature, drop transaction";
        return true;
      }
      else /*amount is 0*/ {
        if(!is_smart_contract(tr)) {
          // not deploy/execute/new_state transaction as well as smart is not payable
          cslog() << name() << ": unable call to payable(), feature is not implemented in contract, drop transaction";
          return true;
        }
      }
    }
    else /*is payable*/ {
      // test if payable() is not directly called
      if( is_executable( tr ) ) {
        const csdb::UserField fld = tr.user_field( cs::trx_uf::start::Methods );
        if( fld.is_valid() ) {
          std::string data = fld.value<std::string>();
          if( !data.empty() ) {
            auto invoke = deserialize<api::SmartContractInvocation>( std::move( data ) );
            if( invoke.method == PayableName ) {
              cslog() << name() << ": unable call to payable() directly, drop transaction";
              return true;
            }
          }
        }
      }
      // contract is payable and transaction addresses it, ok then
      csdebug() << name() << ": allow transaction targeting to payable contract";
    }
  }

  return false;
}

void SmartContracts::on_store_block(csdb::Pool block) {
  // control round-based timeout
  bool retest_required = false;
  for (auto& item : exe_queue) {
    const auto seq = block.sequence();
    if(seq > item.round_start && seq - item.round_start > Consensus::MaxRoundsExecuteSmart) {
      if(item.status == SmartContractStatus::Running || item.status == SmartContractStatus::Finished) {
        cswarning() << name() << ": contract is in queue over " << Consensus::MaxRoundsExecuteSmart
          << " blocks (from #," << item.round_start << "), cancel it without transaction";
        item.close();
        retest_required = true;
      }
      else if(item.status == SmartContractStatus::Closed) {
        retest_required = true;
      }
    }
  }
  if (retest_required) {
    test_exe_queue();
  }

  // inspect transactions against smart contracts, raise special event on every item found:
  if (block.transactions_count() > 0) {
    size_t tr_idx = 0;
    for (const auto& tr : block.transactions()) {
      if (is_smart_contract(tr)) {
        csdebug() << name() << ": smart contract trx #" << block.sequence() << "." << tr_idx;
        // dispatch transaction by its type
        bool is_deploy = this->is_deploy(tr);
        bool is_start = is_deploy ? false : this->is_start(tr);
        if (is_deploy || is_start) {
          if (is_deploy) {
            csdebug() << name() << ": smart contract is deployed, enqueue it for execution";
          }
          else {
            csdebug() << name() << ": smart contract is started, enqueue it for execution";
          }
          enqueue(block, tr_idx);
        }
        else if (is_new_state(tr)) {
          csdebug() << name() << ": smart contract state updated";
          on_new_state(block, tr_idx);
        }
      }
      else if( is_payable_target( tr ) ) {
        // execute payable method
        csdebug() << name() << ": enqueue renewal of smart contract balance";
        enqueue( block, tr_idx );
      }
      ++tr_idx;
    }
  }
}

void SmartContracts::on_read_block(csdb::Pool block, bool* should_stop) {
  if(block.transactions_count() > 0) {
    size_t tr_idx = 0;
    for(const auto& tr : block.transactions()) {
      if(is_new_state(tr)) {
        update_contract_state(tr, false /*force_absolute_address*/);
      }
      else if(is_executable(tr)) {
        // register execution if contract is unknown yet
        csdb::Address addr = tr.target();
        if(known_contracts.count(addr) == 0) {
          StateItem& val = known_contracts[addr];
          SmartContractRef ref(block.hash(), block.sequence(), tr_idx);
          if(is_deploy(tr)) {
            val.deploy = ref;
          }
          else {
            val.execution = ref;
          }
        }
      }
      ++tr_idx;
    }
  }
  *should_stop = false;
}

void SmartContracts::remove_from_queue(std::vector<QueueItem>::const_iterator it)
{
  if(it == exe_queue.cend()) {
    cserror() << name() << ": contract to remove is not in queue";
    return;
  }
  if(it != exe_queue.cbegin()) {
    cswarning() << name() << ": completed contract is not at the top of queue";
  }
  cslog() << "  _____";
  cslog() << " /     \\";
  cslog() << "/   -   \\";
  cslog() << "\\       /";
  cslog() << " \\_____/" << "  " << get_executed_method(it->contract);

  clear_emitted_transactions(it->abs_addr);
  exe_queue.erase(it);
}

// returns false if failed, and caller must remove_from_queue() the item
bool SmartContracts::invoke_execution(const SmartContractRef& contract) {
  csdb::Pool block = bc.loadBlock(contract.sequence);
  if (!block.is_valid()) {
    cserror() << name() << ": load block with smart contract failed, cancel execution";
    return false;
  }
  // call to executor only if currently is trusted
  if (force_execution || (execution_allowed && contains_me(block.confidants()))) {
    csdebug() << name() << ": execute current contract now";
    return execute_async(contract);
  }
  else {
    if (!execution_allowed) {
      csdebug() << name() << ": skip contract execution, it is disabled";
    }
    else {
      csdebug() << name() << ": skip contract execution, not in trusted list";
    }
  }
  // ask caller do not remove item from queue:
  return true;
}

bool SmartContracts::execute(const std::string& invoker, const std::string& smart_address, const api::SmartContractInvocation& contract,
  /*[in,out]*/ SmartExecutionData& data, uint32_t timeout_ms) {

  csdebug() << name() << ": execute " << (contract.method.empty() ? "constructor" : contract.method) << "()";

  executor::ExecuteByteCodeResult result;
  result.status.code = 0;
  try {
    get_api()->getExecutor().executeByteCode(result, invoker, smart_address, contract.smartContractDeploy.byteCodeObjects,
      data.state, contract.method, contract.params, timeout_ms);
  }
  catch(std::exception& x) {
    cserror() << name() << ": " << x.what();
    return false;
  }
  catch(...) {
    cserror() << name() << " exception while executing " << contract.method << "()";
    return false;
  }
  if(result.status.code != 0) {
    return false;
  }
  data.state = result.contractState;
  data.ret_val = result.ret_val;
  return true;
}

bool SmartContracts::execute_payable(const std::string& invoker, const std::string& smart_address, const api::SmartContractInvocation& contract,
  /*[in,out]*/ SmartExecutionData& data, uint32_t timeout_ms, double amount) {

  api::SmartContractInvocation payable = contract;
  payable.method = PayableName;
  payable.params.clear();

  ::general::Variant a0;
  std::ostringstream os0;
  os0 << amount;
  a0.__set_v_string(os0.str());
  payable.params.push_back(a0);
  ::general::Variant a1;
  a1.__set_v_string(std::string("1"));
  payable.params.push_back(a1);

  csdebug() << name() << ": execute " << PayableName << "(" << PayableNameArg0 << " = " << amount << ", " << PayableNameArg1 << " = 1)";
  return execute(invoker, smart_address, payable, data, timeout_ms);
}

// returns false if execution canceled, so caller may call to remove_from_queue()
bool SmartContracts::execute_async(const cs::SmartContractRef& item) {
  csdb::Transaction start_tr = get_transaction(item);
  bool replenish_contract = false; // means indirect call to payable()
  if (!is_executable(start_tr)) {
    replenish_contract = is_payable_target( start_tr );
    if( !replenish_contract ) {
      cserror() << name() << ": unable execute neither deploy nor start transaction";
      return false;
    }
  }
  bool deploy = is_deploy(start_tr);

  csdebug() << name() << ": invoke api to remote executor to "
    << (deploy ? "deploy" : (!replenish_contract ? "execute" : "replenish")) << " contract";

  auto maybe_invoke_info = get_smart_contract(start_tr);
  if (maybe_invoke_info.has_value()) {
    const auto& invoke_info = maybe_invoke_info.value();
    std::string state;
    bool call_payable = false; // only for start or replenish, not for deploy
    double tr_amount = start_tr.amount().to_double();

    const auto it = known_contracts.find(absolute_address(start_tr.target()));
    if(it != known_contracts.cend()) {
      const StateItem& val = it->second;
      if (!deploy) {
        state = val.state;
        call_payable =
          (val.payable == PayableStatus::Implemented && tr_amount > std::numeric_limits<double>::epsilon()) ||
          replenish_contract;
      }
    }
    else {
      cserror() << name() << ": contract state is not found in private table";
    }

    // create runnable object
    auto runnable = [=]() mutable {
      std::string invoker = absolute_address(start_tr.source()).to_api_addr();
      std::string smart_address = absolute_address(start_tr.target()).to_api_addr();
      SmartExecutionData data;
      data.contract_ref = item;
      data.state = state;
      if(invoke_info.smartContractDeploy.byteCodeObjects.empty()) {
        data.error = "unable to execute empty byte code";
        return data;
      }
      if(call_payable) {
        if(!execute_payable(invoker, smart_address, invoke_info, data, Consensus::T_smart_contract >> 1, tr_amount)) {
          data.error = "failed to execute payable()";
          return data;
        }
      }
      // replenish is true only if neither deploy nor start transaction:
      if( !replenish_contract ) {
        if( !execute( invoker, smart_address, invoke_info, data, Consensus::T_smart_contract ) ) {
          if( deploy ) {
            data.error = "failed to deploy contract";
          }
          else {
            data.error = "failed to execute method";
          }
          return data;
        }
      }
      // after deploy should call to payable() if tr_amount > 0,
      // contract has already been pre-registered and payable method is checked:
      if(deploy && call_payable) {
        if(!execute_payable(invoker, smart_address, invoke_info, data, Consensus::T_smart_contract >> 1, tr_amount)) {
          data.error = "failed to execute payable() after deployment";
        }
        else {
          cserror() << name() << ": deployed contract does not implement payable() while transaction amount > 0";
        }
      }
      return data;
    };

    // run async and watch result
    auto watcher = cs::Concurrent::run(cs::RunPolicy::CallQueuePolicy, runnable);
    cs::Connector::connect(&watcher->finished, this, &SmartContracts::on_execute_async_completed);
    executions_.push_back(std::move(watcher));

    return true;
  }
  else {
    cserror() << name() << ": failed get smart contract from transaction";
  }
  return false;
}

void SmartContracts::on_execute_async_completed(const SmartExecutionData& data)
{
  auto it = find_in_queue(data.contract_ref);
  if(it != exe_queue.end()) {
    if(it->status == SmartContractStatus::Finished || it->status == SmartContractStatus::Closed) {
      // already finished (by "timeout"), no transaction required
      return;
    }
    it->finish(bc.getLastSequence());
  }
  else {
    cserror() << name() << ": cannot find in queue just completed contract";
  }

  csdb::Transaction result = result_from_smart_ref(data.contract_ref);
  cs::TransactionsPacket packet;

  if(!data.error.empty()) {
    cserror() << name() << ": " << data.error;
    // result contains empty USRFLD[state::Value]
    result.add_user_field(trx_uf::new_state::Value, std::string {});
    packet.addTransaction(result);
  }
  else {
    csdebug() << name() << ": execution of smart contract is successful";
    result.add_user_field(trx_uf::new_state::Value, data.state);
    result.add_user_field(trx_uf::new_state::RetVal, serialize<decltype(data.ret_val)>(data.ret_val));
    packet.addTransaction(result);

    if(it != exe_queue.end()) {
      std::lock_guard<std::mutex> lock(mtx_emit_transaction);

      if(emitted_transactions.count(it->abs_addr) > 0) {
        const auto& vect = emitted_transactions[it->abs_addr];
        for(const auto& tr : vect) {
          packet.addTransaction(tr);
        }
        csdebug() << name() << ": add " << vect.size() << " emitted trx to contract state";
      }
      else {
        csdebug() << name() << ": no emitted trx added to contract state";
      }
    }
  }

  if(it != exe_queue.end()) {
    clear_emitted_transactions(it->abs_addr);
  }

  set_execution_result(packet);
  checkAllExecutions();
}

csdb::Transaction SmartContracts::result_from_smart_ref(const SmartContractRef& contract) const {
  csdb::Transaction src = get_transaction(contract);
  if (!src.is_valid()) {
    return csdb::Transaction{};
  }

  BlockChain::WalletData wallData{};
  BlockChain::WalletId wallId{};
  if (!bc.findWalletData(SmartContracts::absolute_address(src.target()), wallData, wallId)) {
    return csdb::Transaction{};
  }

  csdb::Transaction result(
      wallData.trxTail_.getLastTransactionId() + 1,
      src.target(),                                  // contracts is source
      src.target(),                                  // contracts is target also
      src.currency(),
      0,                  // amount
      src.max_fee(),      // TODO:: how to calculate max fee?
      src.counted_fee(),  // TODO:: how to calculate fee?
      SolverContext::zeroSignature // empty signature
  );
  // USRFLD1 - ref to start trx
  result.add_user_field(trx_uf::new_state::RefStart, contract.to_user_field());
  // USRFLD2 - total fee
  result.add_user_field(trx_uf::new_state::Fee, 0);

  return result;
}

void SmartContracts::set_execution_result(cs::TransactionsPacket& pack) const {
  cslog() << "  _____";
  cslog() << " /     \\";
  cslog() << "/   =   \\";
  cslog() << "\\  " << std::setw(3) << pack.transactionsCount() << "  /";
  cslog() << " \\_____/";

  if (pack.transactionsCount() > 0) {
    const auto tr = pack.transactions().front();
    csdb::UserField f = tr.user_field(trx_uf::new_state::Value);
    if (f.is_valid()) {
      csdebug() << name() << ": new state size " << f.value<std::string>().size();
    }
    else {
      cserror() << name() << ": trx[0] in packet is not new_state transaction";
    }

    emit signal_smart_executed(pack);
  }
  else {
    cserror() << name() << ": no transactions in execution result pack";
  }
}

// get & handle rejected transactions
// usually ordinary consensus may reject smart-related transactions
void SmartContracts::on_reject(cs::TransactionsPacket& pack) {
  if (exe_queue.empty()) {
    cserror() << name() << ": get rejected smart transactions but execution queue is empty";
    return;
  }

  // will contain "unchanged" states of rejected smart contract calls:
  cs::TransactionsPacket unchanged_pack;

  const auto cnt = pack.transactionsCount();
  if (cnt > 0) {
    csdebug() << name() << ": " << cnt << " trxs are rejected";
    std::vector<csdb::Address> done;
    for (const auto t : pack.transactions()) {
      csdb::Address abs_addr = absolute_address(t.source());
      if (std::find(done.cbegin(), done.cend(), abs_addr) != done.cend()) {
        continue;
      }

      // find source contract in queue
      for (auto it = exe_queue.cbegin(); it != exe_queue.cend(); ++it) {
        if (it->abs_addr == abs_addr) {
          done.push_back(abs_addr);
          csdebug() << name() << ": send to conveyer smart contract failed status (related trxs or state are rejected)";
          // send to consensus
          csdb::Transaction tr = result_from_smart_ref(it->contract);
          // result contains empty USRFLD[state::Value]
          tr.add_user_field(trx_uf::new_state::Value, std::string{});
          if (tr.is_valid()) {
            unchanged_pack.addTransaction(tr);
          }
          else {
            cserror() << name() << ": failed to fix smart contract failure, remove from execution queue";
            remove_from_queue(it);
            test_exe_queue();
          }
          break;
        }
      }

      auto it = known_contracts.find(absolute_address(t.source()));
    }

    if (unchanged_pack.transactionsCount() > 0) {
      Conveyer::instance().addSeparatePacket(unchanged_pack);
    }
  }
  else {
    cserror() << name() << ": trxs are rejected but list is empty";
  }
}

bool SmartContracts::update_contract_state(csdb::Transaction t, bool force_absolute_address /*= true*/)
{
  using namespace trx_uf;
  csdb::UserField fld = t.user_field(new_state::Value);
  if(!fld.is_valid()) {
    cserror() << name() << ": contract state is not updated, transaction does not contain it";
    return false;
  }
  std::string state_value = fld.value<std::string>();
  if(!state_value.empty()) {
    // create or get contract state item
    csdb::Address addr = t.target();
    if(force_absolute_address) {
      addr = absolute_address(addr);
    }
    StateItem& item = known_contracts[addr];
    // update last state (with non-empty one)
    item.state = std::move(state_value);
    // determine it is the result of whether deploy or execute
    fld = t.user_field(new_state::RefStart);
    if(fld.is_valid()) {
      SmartContractRef ref(fld);
      csdb::Transaction t_start = get_transaction(ref);
      if(t_start.is_valid()) {
        if(is_deploy(t_start)) {
          item.deploy = ref;
        }
        else {
          item.execution = ref;
        }
      }
      else {
        cswarning() << name() << ": new_state transaction does not refer to starter one";
      }
    }
  }
  else {
    cswarning() << name() << ": contract state is not updated, new state is empty meaning execution is failed";
    return false;
  }
  return true;
}

bool SmartContracts::is_payable(const csdb::Address abs_addr)
{
  auto item = known_contracts.find(abs_addr);
  if(item == known_contracts.end()) {
    // unknown contract
    return false;
  }
  StateItem& val = item->second;
  if(val.payable != PayableStatus::Unknown) {
    return val.payable == PayableStatus::Implemented;
  }

  // get byte code
  auto maybe_deploy = find_deploy_info(abs_addr);
  if(!maybe_deploy.has_value()) {
    val.payable = PayableStatus::Absent; // to avoid subsequent unsuccessful calls
    return false;
  }
  const auto& deploy = maybe_deploy.value();

  // make blocking call to executor
  if(implements_payable(deploy)) {
    val.payable = PayableStatus::Implemented;
    return true;
  }
  val.payable = PayableStatus::Absent;
  return false;
}

bool SmartContracts::implements_payable(const api::SmartContractInvocation& contract) {
  executor::GetContractMethodsResult result;
  std::string error;

  try {
    get_api()->getExecutor().getContractMethods(result, contract.smartContractDeploy.byteCodeObjects);
  }
  catch(std::exception& x) {
    error = x.what();
  }
  catch(...) {
    error = " exception while executing byte code";
  }

  if(!error.empty()) {
    cserror() << name() << ": " << error;
    // remain payable status unknown for future calls
    return false;
  }

  if(result.status.code != 0) {
    cserror() << name() << ": " << result.status.message;
    // remain payable status unknown for future calls
    return false;
  }

  // lookup payable(amount, currency)
  for(const auto& m : result.methods) {
    if(m.name == PayableName && m.returnType == PayableRetType) {
      if(m.arguments.size() == 2) {
        const auto& a0 = m.arguments[0];
        if(a0.name == PayableNameArg0 && a0.type == PayableArgType) {
          const auto& a1 = m.arguments[1];
          if(a1.name == PayableNameArg1 && a1.type == PayableArgType) {
            return true;
          }
        }
      }
    }
  }

  return false;
}

std::string SmartContracts::get_executed_method(const SmartContractRef & ref)
{
  csdb::Transaction t = get_transaction(ref);
  if(!t.is_valid()) {
    return std::string();
  }
  if(is_executable(t)) {
    const auto maybe_invoke_info = get_smart_contract(t);
    if(!maybe_invoke_info.has_value()) {
      return std::string();
    }
    const auto& invoke_info = maybe_invoke_info.value();
    if(invoke_info.method.empty()) {
      return std::string("constructor()");
    }
    std::ostringstream os;
    os << invoke_info.method << '(';
    size_t cnt_params = 0;
    for(const auto& p : invoke_info.params) {
      if(cnt_params > 0) {
        os << ',';
      }
      p.printTo(os);
      ++cnt_params;
    }
    os << ')';
    return os.str();
  }
  if(is_payable_target(t)) {
    std::ostringstream os;
    os << PayableName << "(" << PayableNameArg0 << " = " << t.amount().to_double() << ", " << PayableNameArg1 << " = 1)";
    return os.str();
  }
}

}  // namespace cs
