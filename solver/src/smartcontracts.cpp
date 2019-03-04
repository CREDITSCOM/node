#include <smartcontracts.hpp>
#include <solvercontext.hpp>

#include <ContractExecutor.h>
#include <csdb/currency.hpp>
#include <csnode/datastream.hpp>
#include <lib/system/logger.hpp>
#include <cscrypto/cryptoconstants.hpp>

#include <memory>
#include <optional>
#include <sstream>
#include <functional>

namespace {
  const char *log_prefix = "Smart: ";
}

namespace cs {

csdb::UserField SmartContractRef::to_user_field() const {
  cs::Bytes data;
  cs::DataStream stream(data);
  stream << hash << sequence << transaction;
  return csdb::UserField(stream.convert<std::string>());
}

void SmartContractRef::from_user_field(const csdb::UserField& fld) {
  std::string data = fld.value<std::string>();
  cs::DataStream stream(data.c_str(), data.size());
  stream >> hash >> sequence >> transaction;
  if (!stream.isValid() || stream.isAvailable(1)) {
    cserror() << "SmartCotractRef: read from malformed user field, abort!";
    hash = csdb::PoolHash{};
    sequence = std::numeric_limits<decltype(sequence)>().max();
    transaction = std::numeric_limits<decltype(transaction)>().max();
  }
}

/*explicit*/
SmartContracts::SmartContracts(BlockChain& blockchain, CallsQueueScheduler& calls_queue_scheduler)
: execution_allowed(true)
, bc(blockchain)
, scheduler(calls_queue_scheduler) {

  // signals subscription (MUST occur AFTER the BlockChains has already subscribed to storage)
  
  // as event receiver:
  cs::Connector::connect(&bc.storeBlockEvent_, this, &SmartContracts::on_store_block);
  cs::Connector::connect(bc.getStorage().read_block_event(), this, &SmartContracts::on_read_block);
  // as event source:
  cs::Connector::connect(&signal_payable_invoke, &bc, &BlockChain::onPayableContractReplenish);
  cs::Connector::connect(&signal_payable_timeout, &bc, &BlockChain::onPayableContractTimeout);
}

SmartContracts::~SmartContracts() = default;

void SmartContracts::init(const cs::PublicKey& id, Node* node)
{
  cs::Lock lock(public_access_lock);

  pnode = node;
  auto connector_ptr = pnode->getConnector();
  if (connector_ptr != nullptr) {
    exec_handler_ptr = connector_ptr->apiExecHandler();
  }
  node_id = id;

  // currently, blockchain is read in such manner that does not require absolute/optimized consolidation post-factum
  // anyway this tested code may become useful in future

  size_t cnt = known_contracts.size();
  // consolidate contract states addressed by public keys with those addressed by wallet ids
  auto pred = [](const auto& item) { return item.first.is_wallet_id(); };
  auto it = std::find_if(known_contracts.cbegin(), known_contracts.cend(), pred);
  while(it != known_contracts.cend()) {
    // non-absolute address item is always newer then absolute one:
    csdb::Address abs_addr = absolute_address(it->first);
    if(abs_addr.is_valid()) {
      const StateItem& opt_out = it->second;
      if(!opt_out.state.empty()) {
        StateItem& updated = known_contracts[abs_addr];
        if(opt_out.ref_deploy.is_valid()) {
          if(updated.ref_deploy.is_valid()) {
            cswarning() << log_prefix << "contract deploy is overwritten by subsequent deploy of the same contract";
          }
          updated.ref_deploy = opt_out.ref_deploy;
          updated.state = opt_out.state;
        }
        if(opt_out.ref_execute.is_valid()) {
          updated.ref_execute = opt_out.ref_execute;
          updated.state = opt_out.state;
        }
      }
      else {
        cswarning() << log_prefix << "empty state stored in contracts states table";
      }
    }
    known_contracts.erase(it);
    it = std::find_if(known_contracts.begin(), known_contracts.end(), pred);
  }

  // validate contract states
  for(const auto& item : known_contracts) {
    const StateItem& val = item.second;
    if(val.state.empty()) {
      cswarning() << log_prefix << "completely unsuccessful contract found, neither deployed, nor executed";
    }
    if(!val.ref_deploy.is_valid()) {
      cswarning() << log_prefix << "unsuccessfully deployed contract found";
    }
  }

  size_t new_cnt = known_contracts.size();
  cslog() << log_prefix << "" << new_cnt << " smart contract states loaded";
  if(cnt > new_cnt) {
    cslog() << log_prefix << "" << cnt - new_cnt << " smart contract state(s) is/are optimizied out";
  }
}

/*static*/
bool SmartContracts::is_smart_contract(const csdb::Transaction& tr) {
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
bool SmartContracts::is_executable(const csdb::Transaction& tr) {
  return SmartContracts::is_smart_contract(tr) && !SmartContracts::is_new_state(tr);
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

  const auto invoke = deserialize<api::SmartContractInvocation>(uf.value<std::string>());
  // deploy ~ start but method in invoke info is empty
  return invoke.method.empty();
}

/*static*/
bool SmartContracts::is_start(const csdb::Transaction& tr) {
  return SmartContracts::is_executable(tr) && !SmartContracts::is_deploy(tr);
}

/*static*/
bool SmartContracts::is_new_state(const csdb::Transaction& tr)
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

  cscrypto::Hash result = cscrypto::calculateHash(strToHash.data(), strToHash.size());

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

/*static*/
bool SmartContracts::has_using_contracts(const csdb::Transaction& tr)
{
  if (!SmartContracts::is_start(tr)) {
    return false;
  }
  return tr.user_field(trx_uf::start::Using).is_valid();
}

/*static*/
std::vector<csdb::Address> SmartContracts::get_using_contracts(const csdb::UserField& fld)
{
  std::vector<csdb::Address> list;
  if (!fld.is_valid()) {
    return list;
  }
  std::string data = fld.value<std::string>();
  cs::DataStream stream(data.c_str(), data.size());
  uint16_t cnt = 0;
  stream >> cnt;
  for (uint16_t i = 0; i < cnt; ++i) {
    uint8_t wallet_id_flag = 0;
    stream >> wallet_id_flag;
    if (wallet_id_flag == 0) {
      cs::PublicKey key;
      if (!stream.isAvailable(key.size())) {
        cserror() << log_prefix << "read using contracts from malformed user field, abort!";
        list.clear();
        return list;
      }
      stream >> key;
      list.emplace_back(csdb::Address::from_public_key(key));
    }
    else {
      csdb::internal::WalletId id;
      if (!stream.isAvailable(sizeof(id))) {
        cserror() << log_prefix << "read using contracts from malformed user field, abort!";
        list.clear();
        return list;
      }
      stream >> id;
      list.emplace_back(csdb::Address::from_wallet_id(id));
    }
  }
  if (!stream.isValid() || stream.isAvailable(1)) {
    cserror() << log_prefix << "read using contracts from malformed user field, abort!";
    list.clear();
  }
  return list;
}

/*static*/
csdb::UserField SmartContracts::set_using_contracts(const std::vector<csdb::Address>& addr_list)
{
  cs::Bytes data;
  cs::DataStream stream(data);
  uint16_t cnt = (uint16_t) addr_list.size();
  stream << cnt;
  for (const auto addr : addr_list) {
    if (addr.is_wallet_id()) {
      stream << (uint8_t) 1 << addr.wallet_id();
    }
    else {
      stream << (uint8_t) 0 << addr.public_key();
    }
  }
  return csdb::UserField(stream.convert<std::string>());

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

std::optional<api::SmartContractInvocation> SmartContracts::find_deploy_info(const csdb::Address& abs_addr) const {
  using namespace trx_uf;
  const auto item = known_contracts.find(abs_addr);
  if (item != known_contracts.cend()) {
    const StateItem& val = item->second;
    if (val.ref_deploy.is_valid()) {
      csdb::Transaction tr_deploy = get_transaction(val.ref_deploy);
      if (tr_deploy.is_valid()) {
        csdb::UserField fld = tr_deploy.user_field(deploy::Code);
        if (fld.is_valid()) {
          std::string data = fld.value<std::string>();
          if (!data.empty()) {
            return std::make_optional(deserialize<api::SmartContractInvocation>(std::move(data)));
          }
        }
      }
    }
  }
  return std::nullopt;
}

bool SmartContracts::is_replenish_contract(const csdb::Transaction& tr)
{
  if(is_smart_contract(tr)) {
    // must not be deploy/execute/new_state transaction
    return false;
  }
  return in_known_contracts(tr.target());
}

std::optional<api::SmartContractInvocation> SmartContracts::get_smart_contract_impl(const csdb::Transaction& tr)
{
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

bool SmartContracts::is_payable_target(const csdb::Transaction& tr) {
  csdb::Address abs_addr = absolute_address(tr.target());
  if(!in_known_contracts(abs_addr)) {
    return false;
  }
  // may do blocking call to API::executor
  return is_payable(abs_addr);
}

void SmartContracts::enqueue(const csdb::Pool& block, size_t trx_idx) {
  if (trx_idx >= block.transactions_count()) {
    cserror() << log_prefix << "incorrect trx index in block to enqueue smart contract";
    return;
  }
  SmartContractRef new_item(block.hash().clone(), block.sequence(), trx_idx);
  
  if (!exe_queue.empty()) {
    auto it = find_in_queue(new_item);
    // test duplicated contract call
    if (it != exe_queue.cend()) {
      cserror() << log_prefix << "attempt to queue duplicated contract {"
        << it->ref_start.sequence << '.' << it->ref_start.transaction << "} transaction, already queued on round #"
        << it->seq_enqueue;
      return;
    }
  }

  // enqueue to end
  csdb::Transaction t = block.transaction(trx_idx);
  csdb::Address abs_addr = absolute_address(t.target());
  bool payable = false;
  if(SmartContracts::is_deploy(t)) {
    // pre-register in known_contracts
    auto maybe_invoke_info = get_smart_contract_impl(t);
    if(maybe_invoke_info.has_value()) {
      const auto& invoke_info = maybe_invoke_info.value();
      StateItem& val = known_contracts[abs_addr];
      val.ref_deploy = new_item;
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

  cslog() << std::endl << log_prefix << "enqueue " << get_executed_method(new_item) << std::endl;
  auto& queue_item = exe_queue.emplace_back(QueueItem(new_item, abs_addr, t));
  update_status(queue_item, new_item.sequence, SmartContractStatus::Waiting);
  queue_item.consumed_fee += smart_round_fee(block); // setup costs of initial round
  queue_item.is_executor = (execution_allowed && contains_me(block.confidants()));
  test_exe_queue();
}

void SmartContracts::on_new_state(const csdb::Pool& block, size_t trx_idx) {
  if (!block.is_valid() || trx_idx >= block.transactions_count()) {
    cserror() << log_prefix << "incorrect new_state transaction specfied";
  }
  else {
    auto new_state = get_transaction(SmartContractRef{block.hash(), block.sequence(), trx_idx});
    if (!new_state.is_valid()) {
      cserror() << log_prefix << "get new_state transaction failed";
    }
    else {
      csdb::UserField fld_contract_ref = new_state.user_field(trx_uf::new_state::RefStart);
      if (!fld_contract_ref.is_valid()) {
        cserror() << log_prefix << "new_state transaction does not contain reference to contract";
      }
      else {
        SmartContractRef contract_ref(fld_contract_ref);
        // update state
        csdebug() << log_prefix << "updating contract state";
        update_contract_state(new_state);
        remove_from_queue(contract_ref);
      }
      csdb::UserField fld_fee = new_state.user_field(trx_uf::new_state::Fee);
      if (fld_fee.is_valid()) {
        cslog() << log_prefix << "contract execution fee " << fld_fee.value<csdb::Amount>().to_double();
        cslog() << log_prefix << "contract new state fee " << new_state.counted_fee().to_double();
      }
    }
  }

  test_exe_queue();
}

void SmartContracts::test_exe_queue() {
  // update queue items status
  auto it = exe_queue.begin();
  while (it != exe_queue.end()) {
    if (it->status == SmartContractStatus::Closed) {
      csdebug() << log_prefix << "finished contract {"
        << it->ref_start.sequence << '.' << it->ref_start.transaction << "} still in queue, remove it";
      it = remove_from_queue(it);
      continue;
    }
    if(it->status == SmartContractStatus::Running) {
      // some contract is already running
      ++it;
      continue;
    }
    if(it->status == SmartContractStatus::Finished) {
      // some contract is under consensus
      ++it;
      continue;
    }
    // status: Waiting
    
    // is locked:
    bool wait_until_unlock = false;
    if (is_locked(it->abs_addr)) {
      csdebug() << log_prefix << "contract {"
        << it->ref_start.sequence << '.' << it->ref_start.transaction << "} still is locked, wait until unlocked";
      wait_until_unlock = true;
    }
    // is anyone of using locked:
    else {
      for (const auto u : it->using_contracts) {
        if (is_locked(absolute_address(u))) {
          csdebug() << log_prefix << "one of using by contract {"
            << it->ref_start.sequence << '.' << it->ref_start.transaction << "} still is locked, wait until unlocked";
          wait_until_unlock = true;
          break;
        }
      }
    }
    if (wait_until_unlock) {
      ++it;
      continue;
    }

    csdebug() << log_prefix << "set running status to contract {" << it->ref_start.sequence << '.' << it->ref_start.transaction << "}";
    update_status(*it, bc.getLastSequence(), SmartContractStatus::Running);
    // call to executor only if is trusted relatively to this contract
    if (it->is_executor) {
      csdebug() << log_prefix << "execute contract {"
        << it->ref_start.sequence << '.' << it->ref_start.transaction << "} now";
      execute_async(it->ref_start, it->avail_fee);
    }
    else {
      csdebug() << log_prefix << "skip contract {"
        << it->ref_start.sequence << '.' << it->ref_start.transaction << "} execution, not in trusted list";
    }

    ++it;
  }
}

SmartContractStatus SmartContracts::get_smart_contract_status(const csdb::Address& addr) const
{
  if (!exe_queue.empty()) {
    const auto it = find_first_in_queue(absolute_address(addr));
    if (it != exe_queue.cend()) {
      return it->status;
    }
  }
  return SmartContractStatus::Idle;
}

bool SmartContracts::capture_transaction(const csdb::Transaction& tr)
{
  cs::Lock lock(public_access_lock);

  // test smart contract as source of transaction
  // the new_state transaction is unable met here, we are the only one source of new_state
  csdb::Address abs_addr = absolute_address(tr.source());
  if(in_known_contracts(abs_addr)) {
    if (!exe_queue.empty()) {
      const auto it = find_first_in_queue(abs_addr);
      if (it != exe_queue.cend()) {
        if (it->status == SmartContractStatus::Running) {
          it->emitted_transactions.push_back(tr.clone());
          csdebug() << log_prefix << "smart contract emits transaction, add, total " << it->emitted_transactions.size();
        }
        else {
          csdebug() << log_prefix << "smart contract is not allowed to emit transaction, drop it";
        }
      }
    }
    return true; // avoid from conveyer sync
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
  
  if(is_contract) {
    // test contract was deployed (and maybe called successfully)
    if (!has_state) {
      cslog() << log_prefix << "unable execute not successfully deployed contract, drop transaction";
      return true; // block from conveyer sync
    }

    double amount = tr.amount().to_double();
    // possible blocking call to executor for the first time:
    if(!is_payable(abs_addr)) {
      if(amount > std::numeric_limits<double>::epsilon()) {
        cslog() << log_prefix << "unable replenish balance of contract without payable() feature, drop transaction";
        return true; // block from conveyer sync
      }
      else /*amount is 0*/ {
        if(!is_smart_contract(tr)) {
          // not deploy/execute/new_state transaction as well as smart is not payable
          cslog() << log_prefix << "unable call to payable(), feature is not implemented in contract, drop transaction";
          return true; // block from conveyer sync
        }
      }
    }
    else /* is payable */ {
      // test if payable() is not directly called
      if( is_executable( tr ) ) {
        const csdb::UserField fld = tr.user_field( cs::trx_uf::start::Methods );
        if( fld.is_valid() ) {
          std::string data = fld.value<std::string>();
          if( !data.empty() ) {
            auto invoke = deserialize<api::SmartContractInvocation>( std::move( data ) );
            if( invoke.method == PayableName ) {
              cslog() << log_prefix << "unable call to payable() directly, drop transaction";
              return true; // block from conveyer sync
            }
          }
        }
        csdebug() << log_prefix << "allow deploy/executable transaction";
      }
      else /* not executable transaction */ {
        // contract is payable and transaction addresses it, ok then
        csdebug() << log_prefix << "allow transaction targeting to payable contract";
      }
    }
  }

  return false; // allow pass to conveyer sync
}

void SmartContracts::on_store_block(const csdb::Pool& block) {

  cs::Lock lock(public_access_lock);

  test_exe_conditions(block);
  test_exe_queue();
  test_contracts_locks();

  // inspect transactions against smart contracts, raise special event on every item found:
  if (block.transactions_count() > 0) {
    size_t tr_idx = 0;
    for (const auto& tr : block.transactions()) {
      if (is_smart_contract(tr)) {
        // dispatch transaction by its type
        bool is_deploy = this->is_deploy(tr);
        bool is_start = is_deploy ? false : this->is_start(tr);
        if (is_deploy || is_start) {
          if (is_deploy) {
            csdebug() << log_prefix << "smart contract is deployed by #" << block.sequence() << "." << tr_idx;
          }
          else {
            csdebug() << log_prefix << "smart contract is called by #" << block.sequence() << "." << tr_idx;
          }
          enqueue(block, tr_idx);
        }
        else if (is_new_state(tr)) {
          csdebug() << log_prefix << "smart contract state is updated by #" << block.sequence() << "." << tr_idx;
          on_new_state(block, tr_idx);
        }
      }
      else if( is_payable_target( tr ) ) {
        // execute payable method
        csdebug() << log_prefix << "smart contract balance is replenished by #" << block.sequence() << "." << tr_idx;
        emit signal_payable_invoke(tr);
        enqueue( block, tr_idx );
      }
      ++tr_idx;
    }
  }
}

void SmartContracts::on_read_block(const csdb::Pool& block, bool* /*should_stop*/) {

  cs::Lock lock(public_access_lock);

  // uncomment when exe_queue is updated during blocks reading on startup:
  //test_exe_conditions(block);

  // control round-based timeout
  // assume block arrive in increasing sequence order
  while(!replenish_contract.empty()) {
    const auto it = replenish_contract.cbegin();
    if(block.sequence() - it->sequence <= Consensus::MaxRoundsCancelContract) {
      // no timeout yet
      break;
    }
    csdb::Transaction t = get_transaction(*it);
    if(t.is_valid()) {
      emit signal_payable_timeout(t);
    }
    replenish_contract.erase(it);
  }

  if(block.transactions_count() > 0) {
    size_t tr_idx = 0;
    for(const auto& tr : block.transactions()) {
      if(is_new_state(tr)) {
        update_contract_state(tr);
      }
      else {
        csdb::Address abs_addr = absolute_address(tr.target());
        if(!abs_addr.is_valid()) {
          cserror() << log_prefix << "failed convert optimized address";
        }
        else {
          if(!in_known_contracts(abs_addr)) {
            if(is_deploy(tr)) {
              // register ONLY contract deploy,
              // known contracts will be updated on new_state handling
              StateItem& val = known_contracts[abs_addr];
              val.ref_deploy.hash = block.hash();
              val.ref_deploy.sequence = block.sequence();
              val.ref_deploy.transaction = tr_idx;
            }
          }
          else {
            if(!is_executable(tr)) {
              // replenish smart contract
              emit signal_payable_invoke(tr);
              replenish_contract.emplace_back(block.hash(), block.sequence(), tr_idx);
            }
          }
        }
      }
      ++tr_idx;
    }
  }

  // do not assign stop flag not to overwrite value set by other subscribers:
  //*should_stop = false;
}

// tests max fee amount and round-based timeout on executed smart contracts;
// invoked on every new block ready
void SmartContracts::test_exe_conditions(const csdb::Pool& block) {
  if (exe_queue.empty()) {
    return;
  }

  const auto seq = block.sequence();
  for (auto& item : exe_queue) {
    if (item.status != SmartContractStatus::Running && item.status != SmartContractStatus::Finished) {
      continue;
    }

    // smart is in executor or is under smart-consensus

    // unconditional timeout, actual for both Finished and Running items
    if (seq > item.seq_start && seq - item.seq_start > Consensus::MaxRoundsCancelContract) {
      cswarning() << log_prefix << "contract {"
        << item.ref_start.sequence << '.' << item.ref_start.transaction << "} is in queue over " << Consensus::MaxRoundsCancelContract
        << " blocks (from #" << item.seq_start << "), remove it without transaction";
      update_status(item, seq, SmartContractStatus::Closed);
      csdb::Transaction starter = get_transaction(item.ref_start);
      if (starter.is_valid()) {
        if (!is_executable(starter)) {
          emit signal_payable_timeout(starter);
        }
      }
      else {
        cserror() << log_prefix << "cannot handle contract {"
          << item.ref_start.sequence << '.' << item.ref_start.transaction << "} execution timeout properly, starter transaction not found";
      }
      continue;
    }

    if (item.status == SmartContractStatus::Running) {
      // test near-timeout:
      if (seq > item.seq_start && seq - item.seq_start > Consensus::MaxRoundsExecuteContract) {
        cslog() << log_prefix << "contract {"
          << item.ref_start.sequence << '.' << item.ref_start.transaction << "} is in queue over " << Consensus::MaxRoundsExecuteContract
          << " blocks (from #" << item.seq_start << "), stop it";
        if (item.is_executor) {
          SmartExecutionData data;
          data.contract_ref = item.ref_start;
          data.error = "contract execution timeout";
          data.result.retValue.__set_v_byte(error::TimeExpired);
          on_execution_completed_impl(data);
        }
        else {
          update_status(item, seq, SmartContractStatus::Finished);
        }
      }
      // test out-of-fee:
      item.consumed_fee += smart_round_fee(block);
      if (item.avail_fee < item.consumed_fee) {
        cslog() << log_prefix << "contract {"
          << item.ref_start.sequence << '.' << item.ref_start.transaction << "} is out of fee, cancel it";
        if (item.is_executor) {
          SmartExecutionData data;
          data.contract_ref = item.ref_start;
          data.error = "contract execution is out of funds";
          data.result.retValue.__set_v_byte(error::OutOfFunds);
          on_execution_completed_impl(data);
        }
        else {
          update_status(item, seq, SmartContractStatus::Finished);
        }
      }
    } // if block for Running only contract

  } // for each exe_queue item
}

// return next element in queue
SmartContracts::queue_iterator SmartContracts::remove_from_queue(SmartContracts::queue_iterator it)
{
  if(it == exe_queue.cend()) {
    cserror() << log_prefix << "contract {"
      << it->ref_start.sequence << '.' << it->ref_start.transaction << "} to remove is not in queue";
  }
  else {
    cslog() << std::endl << log_prefix << "remove from queue completed {"
      << it->ref_start.sequence << '.' << it->ref_start.transaction << "} " << get_executed_method(it->ref_start) << std::endl;
    const cs::Sequence seq = bc.getLastSequence();
    const cs::Sequence seq_cancel = it->seq_start + Consensus::MaxRoundsCancelContract + 1;
    if (seq > it->seq_start + Consensus::MaxRoundsExecuteContract && seq < seq_cancel) {
      cslog() << log_prefix << seq_cancel - seq << " round(s) were to unconditional contract {"
        << it->ref_start.sequence << '.' << it->ref_start.transaction << "} timeout";
    }
    update_lock_status(*it, false);
    it = exe_queue.erase(it);

    if (exe_queue.empty()) {
      csdebug() << log_prefix << "contract queue is empty, nothing to execute";
    }
    else {
      csdebug() << log_prefix << exe_queue.size() << " item(s) in queue";
    }
  }

  return it;
}

bool SmartContracts::execute(SmartExecutionData& data) {
  csdebug() << log_prefix << "execute contract #" << data.contract_ref.sequence << "." << data.contract_ref.transaction;

  csdb::Pool block = bc.loadBlock(data.contract_ref.sequence);
  if (!block.is_valid()) {
    data.error = "load block with starter transaction failed";
    data.result.retValue.__set_v_byte(error::InternalBug);
    return false;
  }
  try {
    auto maybe_result = exec_handler_ptr->getExecutor().executeTransaction(block, data.contract_ref.transaction, data.executor_fee);
    if (maybe_result.has_value()) {
      data.result = maybe_result.value();
    }
    else {
      data.error = "contract execution failed";
      data.result.retValue.__set_v_byte(error::ExecuteTransaction);
    }
    //executeByteCode(result, invoker, smart_address, contract.smartContractDeploy.byteCodeObjects, data.state, contract.method, contract.params, timeout_ms);
  }
  catch (std::exception& x) {
    data.error = x.what();
    data.result.retValue.__set_v_byte(error::StdException);
    return false;
  }
  catch (...) {
    std::ostringstream os;
    os << log_prefix << " unexpected exception while executing contract #" << data.contract_ref.sequence << "." << data.contract_ref.transaction;
    data.error = os.str();
    data.result.retValue.__set_v_byte(error::Exception);
    return false;
  }
  return true;
}

bool SmartContracts::execute_payable(const std::string& /*invoker*/, const std::string& /*smart_address*/,
  const api::SmartContractInvocation& contract, /*[in,out]*/ SmartExecutionData& data, uint32_t /*timeout_ms*/, double amount) {

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

  csdebug() << log_prefix << "execute " << PayableName << "(" << PayableNameArg0 << " = " << os0.str() << ", " << PayableNameArg1 << " = 1)";
  return execute(data);
}

// returns false if execution canceled, so caller may call to remove_from_queue()
bool SmartContracts::execute_async(const cs::SmartContractRef& item, csdb::Amount avail_fee) {
  csdb::Transaction start_tr = get_transaction(item);
  bool replenish_only = false;  // means indirect call to payable()
  if (!is_executable(start_tr)) {
    replenish_only = is_payable_target(start_tr);
    if (!replenish_only) {
      cserror() << log_prefix << "unable execute neither deploy nor start/replenish transaction";
      return false;
    }
  }
  bool deploy = is_deploy(start_tr);

  csdebug() << log_prefix << "invoke api to remote executor to " << (deploy ? "deploy" : (!replenish_only ? "execute" : "replenish")) << " contract";

  auto maybe_invoke_info = get_smart_contract_impl(start_tr);
  if (maybe_invoke_info.has_value()) {
    const auto& invoke_info = maybe_invoke_info.value();
    std::string state;
    bool is_payable = false; // is contract actually payable
    bool call_payable = false;  // only for start or replenish, not for deploy
    double tr_amount = start_tr.amount().to_double();

    const auto it = known_contracts.find(absolute_address(start_tr.target()));
    if (it != known_contracts.cend()) {
      const StateItem& val = it->second;
      is_payable = (val.payable == PayableStatus::Implemented);
      if (!deploy) {
        state = val.state;
        call_payable = (is_payable && tr_amount > std::numeric_limits<double>::epsilon()) || replenish_only;
      }
    }
    else {
      cserror() << log_prefix << "contract state is not found in private table";
    }

    // create runnable object
    auto runnable = [=]() mutable {
      std::string invoker = absolute_address(start_tr.source()).to_api_addr();
      std::string smart_address = absolute_address(start_tr.target()).to_api_addr();
      SmartExecutionData data;
      data.contract_ref = item;
      data.executor_fee = avail_fee;
      if (invoke_info.smartContractDeploy.byteCodeObjects.empty()) {
        data.error = "unable to execute empty byte code";
        return data;
      }
      if (call_payable) {
        if (!execute_payable(invoker, smart_address, invoke_info, data, Consensus::T_smart_contract >> 1, tr_amount)) {
          if (data.error.empty()) {
            data.error = "failed to execute payable()";
          }
          return data;
        }
      }
      // replenish_only is true only if neither deploy nor start transaction: replenish smarts' wallet
      if (!replenish_only) {
        if (!execute(data)) {
          if (data.error.empty()) {
            if (deploy) {
              data.error = "failed to deploy contract";
            }
            else {
              data.error = "failed to execute method";
            }
          }
          return data;
        }
        // on successful execution
        // after deploy should call to payable() if tr_amount > 0,
        // contract has already been pre-registered and payable method is checked:
        if (deploy && tr_amount > std::numeric_limits<double>::epsilon()) {
          if (!is_payable) {
            data.result.retValue.__set_v_byte(error::UnpayableReplenish);
            data.error = "deployed contract does not implement payable() while transaction amount > 0";
          }
          else if (!execute_payable(invoker, smart_address, invoke_info, data, Consensus::T_smart_contract >> 1, tr_amount)) {
            if (data.error.empty()) {
              data.error = "failed to execute payable() after deployment";
            }
          }
        }
      }
      if (data.executor_fee > avail_fee) {
        // out of fee detected
        data.error = "contract execution is out of funds";
        data.result.retValue.__set_v_byte(error::OutOfFunds);
      }
      return data;
    };

    // run async and watch result
    auto watcher = cs::Concurrent::run(cs::RunPolicy::CallQueuePolicy, runnable);
    cs::Connector::connect(&watcher->finished, this, &SmartContracts::on_execution_completed);
    executions_.push_back(std::move(watcher));

    return true;
  }
  else {
    cserror() << log_prefix << "failed get smart contract from transaction";
  }
  return false;
}

void SmartContracts::on_execution_completed_impl(const SmartExecutionData& data) {
  auto it = find_in_queue(data.contract_ref);
  csdb::Transaction result{};
  if (it != exe_queue.end()) {
    if (it->status == SmartContractStatus::Finished || it->status == SmartContractStatus::Closed) {
      // already finished (by timeout), no transaction required
      return;
    }
    csdebug() << log_prefix << "execution of contract {"
      << it->ref_start.sequence << '.' << it->ref_start.transaction << "} has completed";

    update_status(*it, bc.getLastSequence(), SmartContractStatus::Finished);
    
    if (data.result.fee > csdb::Amount(0)) {
      it->consumed_fee = data.result.fee; // executor overrides fee for now
    }

    // add emitted transactions
    if (!data.result.trxns.empty()) {
      for (const auto& t : data.result.trxns) {
        it->emitted_transactions.emplace_back(t);
      }
    }

    result = create_new_state(*it);
  }
  else {
    cserror() << log_prefix << "cannot find in queue just completed contract, so cannot create new_state";
    csdb::Transaction tmp = get_transaction(data.contract_ref);
    if (!tmp.is_valid()) {
      return;
    }
    QueueItem fake(data.contract_ref, absolute_address(tmp.target()), tmp);
    result = create_new_state(fake);
  }

  cs::TransactionsPacket packet;
  if (!data.error.empty()) {
    cserror() << std::endl << log_prefix << data.error << std::endl;
    // result contains empty USRFLD[state::Value]
    result.add_user_field(trx_uf::new_state::Value, std::string{});
    // result contains error code from ret_val
    result.add_user_field(trx_uf::new_state::RetVal, serialize(data.result.retValue));
    packet.addTransaction(result);
  }
  else {
    csdebug() << log_prefix << "execution of smart contract is successful, new state size = " << data.result.newState.size();

    // put new state
    result.add_user_field(trx_uf::new_state::Value, data.result.newState);
    result.add_user_field(trx_uf::new_state::RetVal, serialize(data.result.retValue));
    packet.addTransaction(result);

    // put emitted transactions
    if (it != exe_queue.end()) {
      if (!it->emitted_transactions.empty()) {
        for (const auto& tr : it->emitted_transactions) {
          packet.addTransaction(tr);
        }
        csdebug() << log_prefix << "add " << it->emitted_transactions.size() << " emitted transaction(s) to contract {"
          << it->ref_start.sequence << '.' << it->ref_start.transaction << "} state";
      }
      else {
        csdebug() << log_prefix << "no emitted transaction added to contract state";
      }
    }
  }

  // put new states of using contracts into packet
  if (!data.result.states.empty()) {
    for (const auto& [addr, state] : data.result.states) {
      csdb::Transaction t = create_new_state(*it);
      if (t.is_valid()) {
        // re-assign some fields
        t.set_innerID(next_inner_id(addr));
        t.set_source(addr);
        t.set_target(addr);
        t.add_user_field(trx_uf::new_state::Value, state);
        t.add_user_field(trx_uf::new_state::Fee, csdb::Amount(0));
        packet.addTransaction(t);
      }
    }
  }

  if (it != exe_queue.end()) {
    csdebug() << log_prefix << "starting contract {"
      << it->ref_start.sequence << '.' << it->ref_start.transaction << "} consensus";
    if (!start_consensus(*it, packet)) {
      cserror() << log_prefix << "contract {"
        << it->ref_start.sequence << '.' << it->ref_start.transaction << "} consensus failed, remove item from queue";
      remove_from_queue(it);
    }
  }

  // inform slots if any, packet does not contain smart consensus' data!
  emit signal_smart_executed(packet);

  checkAllExecutions();
}

uint64_t SmartContracts::next_inner_id(const csdb::Address& addr) const
{
  BlockChain::WalletData wallData{};
  BlockChain::WalletId wallId{};
  if (!bc.findWalletData(SmartContracts::absolute_address(addr), wallData, wallId)) {
    return 0;
  }
  return wallData.trxTail_.empty() ? 1 : wallData.trxTail_.getLastTransactionId() + 1;
}

csdb::Transaction SmartContracts::create_new_state(const QueueItem& queue_item) const {
  csdb::Transaction src = get_transaction(queue_item.ref_start);
  if (!src.is_valid()) {
    return csdb::Transaction{};
  }

  BlockChain::WalletData wallData{};
  BlockChain::WalletId wallId{};
  if (!bc.findWalletData(SmartContracts::absolute_address(src.target()), wallData, wallId)) {
    return csdb::Transaction{};
  }

  csdb::Transaction result(
    next_inner_id(src.target()), // see: APIHandler::WalletDataGet(...) in apihandler.cpp
    src.target(),     // contracts is source
    src.target(),     // contracts is target also
    src.currency(),
    0,                // amount
    csdb::AmountCommission((queue_item.avail_fee - queue_item.consumed_fee).to_double()),
    csdb::AmountCommission(queue_item.new_state_fee.to_double()),
    SolverContext::zeroSignature // empty signature
  );
  // USRFLD1 - ref to start trx
  result.add_user_field(trx_uf::new_state::RefStart, queue_item.ref_start.to_user_field());
  // USRFLD2 - total fee
  result.add_user_field(trx_uf::new_state::Fee, queue_item.consumed_fee);
  cslog() << log_prefix << "new_state fee " << result.counted_fee().to_double() << ", contract execution fee " << queue_item.consumed_fee.to_double();
  return result;
}

// get & handle rejected transactions
// usually ordinary consensus may not reject smart-related transactions
void SmartContracts::on_reject(cs::TransactionsPacket& pack) {

  cs::Lock lock(public_access_lock);

  bool retest_queue_required = false;
  // will contain "unchanged" states of rejected smart contract calls:
  cs::TransactionsPacket unchanged_pack;
  const auto cnt = pack.transactionsCount();
  if (cnt == 0) {
    cserror() << log_prefix << "rejected transactions list is empty";
    return;
  }
  csdebug() << log_prefix << "" << cnt << " transactions are rejected";
  std::vector<csdb::Address> done;

  for (const auto t : pack.transactions()) {
    csdb::Address abs_addr = absolute_address(t.source());
    if (std::find(done.cbegin(), done.cend(), abs_addr) != done.cend()) {
      continue;
    }

    // find source contract in queue
    queue_iterator it = find_first_in_queue(abs_addr);
    if (it == exe_queue.end()) {
      cserror() << log_prefix << "cannot find in queue source contract for rejected transactions";
    }
    else {
      done.push_back(abs_addr);
      csdebug() << log_prefix << "send to conveyer contract {"
        << it->ref_start.sequence << '.' << it->ref_start.transaction << "} failed status (emitted transactions or state are rejected)";
      // send to consensus
      csdb::Transaction tr = create_new_state(*it);
      // result contains empty USRFLD[state::Value]
      tr.add_user_field(trx_uf::new_state::Value, std::string{});
      // result contains error code "rejected by consensus"
      using namespace cs::trx_uf;
      ::general::Variant err_code;
      err_code.__set_v_byte(error::ConsensusRejected);
      tr.add_user_field(trx_uf::new_state::RetVal, serialize(err_code));
      if (tr.is_valid()) {
        unchanged_pack.addTransaction(tr);
      }
      else {
        cserror() << log_prefix << "failed to store contract {"
          << it->ref_start.sequence << '.' << it->ref_start.transaction << "} failure, remove from execution queue";
        update_status(*it, bc.getLastSequence(), SmartContractStatus::Closed);
        retest_queue_required = true;
      }
    }
  }

  if (unchanged_pack.transactionsCount() > 0) {
    Conveyer::instance().addSeparatePacket(unchanged_pack);
  }

  if (retest_queue_required) {
    test_exe_queue();
  }
}

bool SmartContracts::update_contract_state(const csdb::Transaction& t) {
  using namespace trx_uf;
  csdb::UserField fld = t.user_field(new_state::Value);
  if (!fld.is_valid()) {
    cserror() << log_prefix << "contract state is not updated, transaction does not contain it";
    return false;
  }
  std::string state_value = fld.value<std::string>();
  if (!state_value.empty()) {
    // create or get contract state item
    csdb::Address abs_addr = absolute_address(t.target());
    if (abs_addr.is_valid()) {
      StateItem& item = known_contracts[abs_addr];
      // update last state (with non-empty one)
      item.state = std::move(state_value);
      // determine it is the result of whether deploy or execute
      fld = t.user_field(new_state::RefStart);
      if (fld.is_valid()) {
        SmartContractRef ref(fld);
        csdb::Transaction t_start = get_transaction(ref);
        if (t_start.is_valid()) {
          if (is_executable(t_start)) {
            if (is_deploy(t_start)) {
              item.ref_deploy = ref;
            }
            else {
              item.ref_execute = ref;
            }
          }
          else {
            // handle replenish during startup reading
            if (!replenish_contract.empty()) {
              const auto it = std::find(replenish_contract.cbegin(), replenish_contract.cend(), ref);
              if (it != replenish_contract.cend()) {
                replenish_contract.erase(it);
              }
            }
            // handle replenish from on-the-air blocks
            if (item.payable != PayableStatus::Implemented) {
              cserror() << log_prefix << "non-payable contract state is updated by replenish transaction";
            }
            item.ref_execute = ref;
          }
        }
        else {
          cswarning() << log_prefix << "new_state transaction does not refer to starter one";
        }
      }
    }
    else {
      cserror() << log_prefix << "failed to convert optimized address";
    }
  }
  else {
    // state_value is empty - erase replenish_contract item if exists
    if (!replenish_contract.empty()) {
      fld = t.user_field(new_state::RefStart);
      if (fld.is_valid()) {
        SmartContractRef ref(fld);
        csdb::Transaction t_start = get_transaction(ref);
        if (t_start.is_valid()) {
          // handle replenish during startup reading
          const auto it = std::find(replenish_contract.cbegin(), replenish_contract.cend(), ref);
          if (it != replenish_contract.cend()) {
            replenish_contract.erase(it);
          }
        }
      }
    }
    cswarning() << log_prefix << "contract state is not updated, new state is empty meaning execution is failed";
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
  const StateItem& val = item->second;
  if (val.payable != PayableStatus::Unknown) {
    return val.payable == PayableStatus::Implemented;
  }

  // the first time test
  auto maybe_deploy = find_deploy_info(abs_addr);
  PayableStatus result = PayableStatus::Unknown;
  if (!maybe_deploy.has_value()) {
    result = PayableStatus::Absent;
  }
  else {
    if (implements_payable(maybe_deploy.value())) {
      result = PayableStatus::Implemented;
    }
    else {
      result = PayableStatus::Absent;
    }
  }

  item->second.payable = result;
  return result == PayableStatus::Implemented;
}

bool SmartContracts::implements_payable(const api::SmartContractInvocation& contract) {
  executor::GetContractMethodsResult result;
  std::string error;

  try {
    exec_handler_ptr->getExecutor().getContractMethods(result, contract.smartContractDeploy.byteCodeObjects);
  }
  catch (std::exception& x) {
    error = x.what();
  }
  catch (...) {
    error = " exception while executing byte code";
  }

  if (!error.empty()) {
    cserror() << log_prefix << "" << error;
    // remain payable status unknown for future calls
    return false;
  }

  if (result.status.code != 0) {
    cserror() << log_prefix << "" << result.status.message;
    // remain payable status unknown for future calls
    return false;
  }

  // lookup payable(amount, currency)
  for (const auto& m : result.methods) {
    if (m.name == PayableName && m.returnType == PayableRetType) {
      if (m.arguments.size() == 2) {
        const auto& a0 = m.arguments[0];
        if (a0.name == PayableNameArg0 && a0.type == PayableArgType) {
          const auto& a1 = m.arguments[1];
          if (a1.name == PayableNameArg1 && a1.type == PayableArgType) {
            return true;
          }
        }
      }
    }
  }

  return false;
}

std::string SmartContracts::get_executed_method(const SmartContractRef& ref) {
  csdb::Transaction t = get_transaction(ref);
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
      p.printTo(os);
      ++cnt_params;
    }
    os << ')';
    return os.str();
  }
  if (is_payable_target(t)) {
    std::ostringstream os;
    os << PayableName << "(" << PayableNameArg0 << " = " << t.amount().to_double() << ", " << PayableNameArg1 << " = 1)";
    return os.str();
  }
  return std::string("???");
}

csdb::Amount SmartContracts::smart_round_fee(const csdb::Pool& block)
{
  csdb::Amount fee(0);
  if (block.transactions_count() > 0) {
    for (const auto& t : block.transactions()) {
      fee += csdb::Amount(t.counted_fee().to_double());
    }
  }
  return fee;
}

void SmartContracts::update_status(QueueItem & item, cs::RoundNumber r, SmartContractStatus status)
{
  item.status = status;

  switch (status) {
    case SmartContractStatus::Waiting:
      item.seq_enqueue = r;
      csdebug() << log_prefix << "contract {"
        << item.ref_start.sequence << '.' << item.ref_start.transaction << "} is waiting from #" << r;
      break;
    case SmartContractStatus::Running:
      item.seq_start = r;
      update_lock_status(item, true);
      csdebug() << log_prefix << "contract {"
        << item.ref_start.sequence << '.' << item.ref_start.transaction << "} is running from #" << r;
      break;
    case SmartContractStatus::Finished:
      item.seq_finish = r;
      csdebug() << log_prefix << "contract {"
        << item.ref_start.sequence << '.' << item.ref_start.transaction << "} is finished on #" << r;
      break;
    case SmartContractStatus::Closed:
      update_lock_status(item, false);
      csdebug() << log_prefix << "contract {"
        << item.ref_start.sequence << '.' << item.ref_start.transaction << "} is closed";
      break;
    default:
        break;
  }
}

void SmartContracts::test_contracts_locks()
{
  if (exe_queue.empty()) {
    for (auto& item : known_contracts) {
      if (item.second.is_locked) {
        item.second.is_locked = false;
        cslog() << log_prefix << "find locked contract which is not executed now, unlock";
      }
    }
  }
}

}  // namespace cs
