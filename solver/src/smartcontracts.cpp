#include <smartcontracts.hpp>
#include <solvercontext.hpp>

#include <ContractExecutor.h>
#include <csdb/currency.hpp>
#include <csnode/datastream.hpp>
#include <lib/system/logger.hpp>

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
  pnode = node;
  if(pnode->getConnector()!=nullptr) {
    papi = (pnode->getConnector())->apiHandler();
  }
  node_id = id;

  // currently, blockchain is read in such manner that does not require absolute/optimized consolidation post-factum
  // anyway this tested code may become useful in future

  std::unique_lock lock_write(known_contracts_lock);

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

  lock_write.unlock();
  std::shared_lock lock_read(known_contracts_lock);

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

  const auto invoke = deserialize<api::SmartContractInvocation>(uf.value<std::string>());  // get_smart_contract(tr);
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
  std::shared_lock lock_read(known_contracts_lock);

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
  return is_known_smart_contract(tr.target());
}

std::optional<api::SmartContractInvocation> SmartContracts::get_smart_contract(const csdb::Transaction& tr) {
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
  if(!is_known_smart_contract(abs_addr)) {
    return false;
  }
  // may do blocking call to API::executor
  return is_payable(abs_addr);
}

void SmartContracts::clear_emitted_transactions(const csdb::Address& abs_addr) {
  cs::Lock lock(emit_transaction_lock);

  if(emitted_transactions.count(abs_addr) > 0) {
    emitted_transactions.erase(abs_addr);
  }
}

//  const csdb::PoolHash blk_hash, cs::Sequence blk_seq, size_t trx_idx, cs::RoundNumber round
void SmartContracts::enqueue(const csdb::Pool& block, size_t trx_idx) {
  if (trx_idx >= block.transactions_count()) {
    cserror() << log_prefix << "incorrect trx index in block to enqueue smart contract";
    return;
  }
  SmartContractRef new_item(block.hash().clone(), block.sequence(), trx_idx);
  
  {
    std::lock_guard l(exe_queue_lock);
    if (!exe_queue.empty()) {
      auto it = find_in_queue(new_item);
      // test duplicated contract call
      if (it != exe_queue.cend()) {
        cserror() << log_prefix << "attempt to queue duplicated contract {" << it->seq_enqueue << "} transaction, already queued on round #"
          << it->seq_enqueue;
        return;
      }
    }
  }

  // enqueue to end
  csdb::Transaction t = block.transaction(trx_idx);
  csdb::Address abs_addr = absolute_address(t.target());
  bool payable = false;
  if(is_deploy(t)) {
    // pre-register in known_contracts
    auto maybe_invoke_info = get_smart_contract(t);
    if(maybe_invoke_info.has_value()) {
      const auto& invoke_info = maybe_invoke_info.value();

      std::unique_lock lock_write(known_contracts_lock);
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
  {
    std::lock_guard l(exe_queue_lock);
    auto& queue_item = exe_queue.emplace_back(QueueItem(new_item, abs_addr, t));
    queue_item.wait(new_item.sequence);
    queue_item.consumed_fee += smart_round_fee(block); // setup costs of initial round
    queue_item.is_executor = (execution_allowed && contains_me(block.confidants()));
  }
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

  std::lock_guard l(exe_queue_lock);
  
  // update queue items status
  auto it = exe_queue.begin();
  while (it != exe_queue.end()) {
    if (it->status == SmartContractStatus::Closed) {
      csdebug() << log_prefix << "finished contract {" << it->seq_enqueue << "} still in queue, remove it";
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
      break;
    }
    // status: Waiting
    const::csdb::Address lookup_abs_addr = it->abs_addr;
    auto it_locked = std::find_if(exe_queue.cbegin(), exe_queue.cend(),
      [this, lookup_abs_addr](const QueueItem& item) {return item.abs_addr ==lookup_abs_addr && item.status != SmartContractStatus::Waiting; });
    if (it_locked != exe_queue.cend()) {
      csdebug() << log_prefix << "contract {" << it->seq_enqueue << "} still is locked, wait to execute";
      ++it;
      continue;
    }
    csdebug() << log_prefix << "set running status to contract {" << it->seq_enqueue << "}";
    clear_emitted_transactions(it->abs_addr); // insurance from obsolete transactions
    it->start(bc.getLastSequence()); // use blockchain based round counting
    // call to executor only if is trusted relatively to this contract
    if (it->is_executor) {
      csdebug() << log_prefix << "execute contract {" << it->seq_enqueue << "} now";
      execute_async(it->ref_start);
    }
    else {
      csdebug() << log_prefix << "skip contract {" << it->seq_enqueue << "} execution, not in trusted list";
    }

    ++it;
  }
}

SmartContractStatus SmartContracts::get_smart_contract_status(const csdb::Address& addr) const
{
  std::lock_guard l(exe_queue_lock);

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
  // test smart contract as source of transaction
  // the new_state transaction is unable met here, we are the only one source of new_state
  if(is_known_smart_contract(tr.source())) {
    csdb::Address abs_addr = absolute_address(tr.source());
    if(is_running_smart_contract(abs_addr)) {
      // expect calls from api when trxs received
      cs::Lock lock(emit_transaction_lock);

      auto& vect = emitted_transactions[abs_addr];
      vect.push_back(tr.clone());
      csdebug() << log_prefix << "smart contract emits transaction, add, total " << vect.size();
    }
    else {
      csdebug() << log_prefix << "smart contract is not allowed to emit transaction, drop it";
    }
    return true; // block from conveyer sync
  }

  // test smart contract as target of transaction (is it payable?)
  csdb::Address abs_addr = absolute_address(tr.target());
  bool is_contract = false;
  bool has_state = false;

  {
    std::shared_lock lock_read(known_contracts_lock);
    const auto it = known_contracts.find(abs_addr);
    if (it != known_contracts.end()) {
      is_contract = true;
      has_state = !it->second.state.empty();
    }
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
  test_exe_conditions(block);
  test_exe_queue();
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

void SmartContracts::on_read_block(const csdb::Pool& block, bool* should_stop) {
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
          if(!is_known_smart_contract(abs_addr)) {
            if(is_deploy(tr)) {
              // register ONLY contract deploy,
              // known contracts will be updated on new_state handling
              std::unique_lock lock_write(known_contracts_lock);
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

  std::lock_guard l(exe_queue_lock);

  if(!exe_queue.empty()) {
    for(auto& item : exe_queue) {
      // if smart is in executor or is under smart-consensus:
      if(item.status == SmartContractStatus::Running || item.status == SmartContractStatus::Finished) {
        // test out-of-fee:
        item.consumed_fee += smart_round_fee(block);
        if(item.avail_fee - item.consumed_fee <= item.new_state_fee) {
          cswarning() << log_prefix << "contract {" << item.seq_enqueue << "} is out of fee, cancel it";
          SmartExecutionData data;
          data.contract_ref = item.ref_start;
          data.error = "contract execution is out of funds";
          data.ret_val.__set_v_byte(error::OutOfFunds);
          on_execution_completed(data);
          continue;
        }
        // round-based timeout
        const auto seq = block.sequence();
        if (seq > item.seq_start) {
          size_t delta = seq - item.seq_start;
          if (delta > Consensus::MaxRoundsCancelContract) {
            cswarning() << log_prefix << "contract {" << item.seq_enqueue << "} is in queue over " << Consensus::MaxRoundsCancelContract
              << " blocks (from #" << item.seq_start << "), remove it without transaction";
            item.close();
          }
          else if (item.status == SmartContractStatus::Running && delta > Consensus::MaxRoundsExecuteContract && item.is_executor) {
            cslog() << log_prefix << "contract {" << item.seq_enqueue << "} is in queue over " << Consensus::MaxRoundsExecuteContract
              << " blocks (from #" << item.seq_start << "), cancel it";
            if (item.is_executor) {
              SmartExecutionData data;
              data.contract_ref = item.ref_start;
              data.error = "contract execution timeout";
              data.ret_val.__set_v_byte(error::TimeExpired);
              on_execution_completed(data);
            }
            else {
              item.finish(seq);
            }
            continue;
          }
        }
        // if item has just been closed:
        if(item.status == SmartContractStatus::Closed) {
          csdb::Transaction starter = get_transaction(item.ref_start);
          if(starter.is_valid()) {
            emit signal_payable_timeout(starter);
          }
          else {
            cserror() << log_prefix << "cannot handle contract {" << item.seq_enqueue << "} execution timeout";
          }
        }
      }
    }
  }
}

// return next element in queue
SmartContracts::queue_iterator SmartContracts::remove_from_queue(SmartContracts::queue_iterator it)
{
  std::lock_guard l(exe_queue_lock);

  if(it == exe_queue.cend()) {
    cserror() << log_prefix << "contract {" << it->seq_enqueue << "} to remove is not in queue";
  }
  else {
    cslog() << std::endl << log_prefix << "remove from queue completed {" << it->seq_enqueue << "} " << get_executed_method(it->ref_start) << std::endl;
    clear_emitted_transactions(it->abs_addr);
    const cs::Sequence seq = bc.getLastSequence();
    const cs::Sequence seq_cancel = it->seq_start + Consensus::MaxRoundsCancelContract + 1;
    if (seq > it->seq_start + Consensus::MaxRoundsExecuteContract && seq < seq_cancel) {
      cslog() << log_prefix << seq_cancel - seq << " round(s) were to unconditional contract {" << it->seq_enqueue << "} timeout";
    }
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

bool SmartContracts::execute(const std::string& invoker, const std::string& smart_address, const api::SmartContractInvocation& contract,
                             /*[in,out]*/ SmartExecutionData& data, uint32_t timeout_ms) {
  csdebug() << log_prefix << "execute " << (contract.method.empty() ? "constructor" : contract.method) << "()";

  executor::ExecuteByteCodeResult result;
  result.status.code = 0;
  try {
    get_api()->getExecutor().executeByteCode(result, invoker, smart_address, contract.smartContractDeploy.byteCodeObjects, data.state, contract.method, contract.params,
                                             timeout_ms);
  }
  catch (std::exception& x) {
    data.error = x.what();
    data.ret_val.__set_v_byte(error::StdException);
    return false;
  }
  catch (...) {
    std::ostringstream os;
    os << log_prefix << " exception while executing " << contract.method << "()";
    data.error = os.str();
    data.ret_val.__set_v_byte(error::Exception);
    return false;
  }
  if (result.status.code != 0) {
    data.error = result.status.message;
    data.ret_val.__set_v_byte(result.status.code);
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

  csdebug() << log_prefix << "execute " << PayableName << "(" << PayableNameArg0 << " = " << os0.str() << ", " << PayableNameArg1 << " = 1)";
  return execute(invoker, smart_address, payable, data, timeout_ms);
}

// returns false if execution canceled, so caller may call to remove_from_queue()
bool SmartContracts::execute_async(const cs::SmartContractRef& item) {
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

  auto maybe_invoke_info = get_smart_contract(start_tr);
  if (maybe_invoke_info.has_value()) {
    const auto& invoke_info = maybe_invoke_info.value();
    std::string state;
    bool is_payable = false; // is contract actually payable
    bool call_payable = false;  // only for start or replenish, not for deploy
    double tr_amount = start_tr.amount().to_double();

    {
      std::shared_lock lock_read(known_contracts_lock);
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
    }

    // create runnable object
    auto runnable = [=]() mutable {
      std::string invoker = absolute_address(start_tr.source()).to_api_addr();
      std::string smart_address = absolute_address(start_tr.target()).to_api_addr();
      SmartExecutionData data;
      data.contract_ref = item;
      data.state = state;
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
        if (!execute(invoker, smart_address, invoke_info, data, Consensus::T_smart_contract)) {
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
            data.ret_val.__set_v_byte(error::UnpayableReplenish);
            data.error = "deployed contract does not implement payable() while transaction amount > 0";
          }
          else if (!execute_payable(invoker, smart_address, invoke_info, data, Consensus::T_smart_contract >> 1, tr_amount)) {
            if (data.error.empty()) {
              data.error = "failed to execute payable() after deployment";
            }
          }
        }
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

void SmartContracts::on_execution_completed(const SmartExecutionData& data) {
  
  std::lock_guard l(exe_queue_lock);

  auto it = find_in_queue(data.contract_ref);
  csdb::Transaction result{};
  if (it != exe_queue.end()) {
    if (it->status == SmartContractStatus::Finished || it->status == SmartContractStatus::Closed) {
      // already finished (by timeout), no transaction required
      return;
    }
    csdebug() << log_prefix << "executor has returned call to contract {" << it->seq_enqueue << "}";
    it->finish(bc.getLastSequence());
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
    result.add_user_field(trx_uf::new_state::RetVal, serialize(data.ret_val));
    packet.addTransaction(result);
  }
  else {
    csdebug() << log_prefix << "execution of smart contract is successful, new state size = " << data.state.size();
    result.add_user_field(trx_uf::new_state::Value, data.state);
    result.add_user_field(trx_uf::new_state::RetVal, serialize(data.ret_val));
    packet.addTransaction(result);

    if (it != exe_queue.end()) {
      cs::Lock lock(emit_transaction_lock);

      if (emitted_transactions.count(it->abs_addr) > 0) {
        const auto& vect = emitted_transactions[it->abs_addr];
        for (const auto& tr : vect) {
          packet.addTransaction(tr);
        }
        csdebug() << log_prefix << "add " << vect.size() << " emitted transaction(s) to contract {" << it->seq_enqueue << "} state";
      }
      else {
        csdebug() << log_prefix << "no emitted transaction added to contract state";
      }
    }
  }

  if (it != exe_queue.end()) {
    clear_emitted_transactions(it->abs_addr);

    csdebug() << log_prefix << "starting contract {" << it->seq_enqueue << "} consensus";
    if (!it->start_consensus(packet, pnode, this)) {
      cserror() << log_prefix << "contract {" << it->seq_enqueue << "} consensus failed, remove item from queue";
      remove_from_queue(it);
    }
  }

  // inform slots if any, packet does not contain smart consensus' data!
  emit signal_smart_executed(packet);

  checkAllExecutions();
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
    wallData.trxTail_.empty() ? 1 : wallData.trxTail_.getLastTransactionId() + 1, // see: APIHandler::WalletDataGet(...) in apihandler.cpp
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
    std::lock_guard l(exe_queue_lock);

    queue_iterator it = find_first_in_queue(abs_addr);
    if (it == exe_queue.end()) {
      cserror() << log_prefix << "cannot find in queue source contract for rejected transactions";
    }
    else {
      done.push_back(abs_addr);
      csdebug() << log_prefix << "send to conveyer contract {" << it->seq_enqueue << "} failed status (emitted transactions or state are rejected)";
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
        cserror() << log_prefix << "failed to store contract {" << it->seq_enqueue << "} failure, remove from execution queue";
        it->close();
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

      std::unique_lock lock_write(known_contracts_lock);

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

  {
    // the most frequent fast test
    std::shared_lock lock_read(known_contracts_lock);
    auto item = known_contracts.find(abs_addr);
    if (item == known_contracts.end()) {
      // unknown contract
      return false;
    }
    const StateItem& val = item->second;
    if (val.payable != PayableStatus::Unknown) {
      return val.payable == PayableStatus::Implemented;
    }
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

  std::unique_lock lock_write(known_contracts_lock);
  auto item = known_contracts.find(abs_addr);
  if (item != known_contracts.end()) {
    item->second.payable = result;
  }
  return result == PayableStatus::Implemented;
}

bool SmartContracts::implements_payable(const api::SmartContractInvocation& contract) {
  executor::GetContractMethodsResult result;
  std::string error;

  try {
    get_api()->getExecutor().getContractMethods(result, contract.smartContractDeploy.byteCodeObjects);
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
    const auto maybe_invoke_info = get_smart_contract(t);
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

}  // namespace cs
