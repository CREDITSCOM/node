#include <smartcontracts.hpp>
#include <solvercontext.hpp>

#include <lib/system/logger.hpp>
#include <csdb/currency.hpp>
#include <ContractExecutor.h>
#include <csnode/datastream.hpp>

#include <optional>
#include <memory>

namespace cs
{
  csdb::UserField SmartContractRef::to_user_field() const
  {
    cs::Bytes data;
    cs::DataStream stream(data);
    stream << hash << sequence << transaction;
    return csdb::UserField(std::string(data.cbegin(), data.cend()));
  }

  void SmartContractRef::from_user_field(csdb::UserField fld)
  {
    std::string data = fld.value<std::string>();
    cs::DataStream stream(data.c_str(), data.size());
    stream >> hash >> sequence >> transaction;
    if(!stream.isValid() || stream.isAvailable(1)) {
      cserror() << "SmartCotractRef: read form malformed user field, abort!";
      hash = csdb::PoolHash {};
      sequence = std::numeric_limits<decltype(sequence)>().max();
      transaction = std::numeric_limits<decltype(transaction)>().max();
    }
  }

  /*explicit*/
  SmartContracts::SmartContracts(BlockChain& blockchain, CallsQueueScheduler& calls_queue_scheduler)
    : execution_allowed(true)
    , force_execution(false)
    , bc(blockchain)
    , scheduler(calls_queue_scheduler)
  {
#if defined(DEBUG_SMARTS)
    execution_allowed = false;
#endif

    // signals subscription
    cs::Connector::connect(&bc.storeBlockEvent_, this, &SmartContracts::onStoreBlock);
    cs::Connector::connect(bc.getStorage().read_block_event(), this, &SmartContracts::onReadBlock);
  }

  SmartContracts::~SmartContracts() = default;

  void SmartContracts::init(const cs::PublicKey& id, csconnector::connector::ApiHandlerPtr api)
  {
    papi = api;
    node_id = id;
  }

  /*static*/
  bool SmartContracts::is_smart_contract(const csdb::Transaction tr)
  {
    if(!tr.is_valid()) {
      return false;
    }
    // to contain smart contract trx must contain either FLD[0] (deploy, start) or FLD[-2] (new_state), both of type "String":
    csdb::UserField f = tr.user_field(trx_uf::deploy::Code);
    if(!f.is_valid()) {
      f = tr.user_field(trx_uf::new_state::Value);
    }
    return f.is_valid() && f.type() == csdb::UserField::Type::String;
  }

  bool SmartContracts::is_executable(const csdb::Transaction tr) const
  {
    if(!is_smart_contract(tr)) {
      return false;
    }
    if(is_new_state(tr)) {
      return false;
    }
    return true;
  }

  bool SmartContracts::is_deploy(const csdb::Transaction tr) const
  {
    if(!is_executable(tr)) {
      return false;
    }

    using namespace cs::trx_uf;
    csdb::UserField uf = tr.user_field(deploy::Code);
    if(!uf.is_valid()) {
      return false;
    }

    const auto invoke = deserialize<api::SmartContractInvocation>(uf.value<std::string>()); //get_smart_contract(tr);
    // deploy ~ start but method in invoke info is empty
    return invoke.method.empty();
  }

  bool SmartContracts::is_start(const csdb::Transaction tr) const
  {
    if(!is_executable(tr)) {
      return false;
    }

    const auto invoke = get_smart_contract(tr);
    if(!invoke.has_value()) {
      return false;
    }
    // deploy ~ start but method in invoke info is empty
    return !invoke.value().method.empty();
  }

  /* static */
  /* Assuming deployer.is_public_key() */
  csdb::Address SmartContracts::get_valid_smart_address(const csdb::Address& deployer,
    const uint64_t trId,
    const api::SmartContractDeploy& data)
  {
    static_assert(cscrypto::kHashSize == cscrypto::kPublicKeySize);

    std::vector<cscrypto::Byte> strToHash;
    std::string byteCode{};
    if (!data.byteCodeObjects.empty()) {
      for (auto &curr_byteCode : data.byteCodeObjects) {
        byteCode += curr_byteCode.byteCode;
      }
    }
    strToHash.reserve(cscrypto::kPublicKeySize + 6 + byteCode.size());

    const auto dPk = deployer.public_key();
    const auto idPtr = reinterpret_cast<const cscrypto::Byte*>(&trId);

    std::copy(dPk.begin(), dPk.end(), std::back_inserter(strToHash));
    std::copy(idPtr, idPtr + 6, std::back_inserter(strToHash));
    std::copy(byteCode.begin(),
      byteCode.end(),
      std::back_inserter(strToHash));

    cscrypto::Hash result;
    cscrypto::CalculateHash(result, strToHash.data(), strToHash.size());

    return csdb::Address::from_public_key(reinterpret_cast<char*>(result.data()));
  }

  bool SmartContracts::is_new_state(const csdb::Transaction tr) const
  {
    if(!is_smart_contract(tr)) {
      return false;
    }
    // must contain user field trx_uf::new_state::Value
    return tr.user_field_ids().count(trx_uf::new_state::Value) > 0;
  }

  /*static*/
  csdb::Transaction SmartContracts::get_transaction(BlockChain& storage, const SmartContractRef& contract)
  {
    csdb::Pool block = storage.loadBlock(contract.sequence);
    if(!block.is_valid()) {
      return csdb::Transaction {};
    }
    if(contract.transaction >= block.transactions_count()) {
      return csdb::Transaction {};
    }
    return block.transactions().at(contract.transaction);
  }

  void SmartContracts::onExecutionFinished(const SmartExecutionData& data) {
    auto it = find_in_queue(data.smartContract);
    if(it != exe_queue.end()) {
      if(it->status == SmartContractStatus::Finished) {
        // already finished (by "timeout"), no transaction required
        return;
      }
      it->status = SmartContractStatus::Finished;
    }
    else {
      cserror() << name() << ": cannot find in queue just completed contract";
    }

    csdb::Transaction result = result_from_smart_ref(data.smartContract);
    cs::TransactionsPacket packet;

    if(data.result.status.code != 0) {
      cserror() << name() << ": failed to execute smart contract";
      if(it != exe_queue.end()) {

        std::lock_guard<std::mutex> lock(mtx_emit_transaction);

        if(!it->created_transactions.empty()) {
          cswarning() << name() << ": drop " << it->created_transactions.size() << " emitted trx";
          it->created_transactions.clear();
        }
      }
      // result contains empty USRFLD[state::Value]
      result.add_user_field(trx_uf::new_state::Value, std::string {});
      packet.addTransaction(result);
    }
    else {
      csdebug() << name() << ": execution of smart contract is successful";
      result.add_user_field(trx_uf::new_state::Value, data.result.contractState);
      result.add_user_field(trx_uf::new_state::RetVal, serialize<decltype(data.result.ret_val)>(data.result.ret_val));
      packet.addTransaction(result);

      std::lock_guard<std::mutex> lock(mtx_emit_transaction);

      if(it != exe_queue.end() && !it->created_transactions.empty()) {
        for(const auto& tr : it->created_transactions) {
          packet.addTransaction(tr);
        }
        csdebug() << name() << ": add " << it->created_transactions.size() << " emitted trx to contract state";
      }
      else {
        csdebug() << name() << ": no emitted trx added to contract state";
      }
    }

    set_execution_result(packet);
    checkAllExecutions();
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

  std::optional<api::SmartContractInvocation> SmartContracts::get_smart_contract(const csdb::Transaction tr) const
  {
    // currently calls to is_***() from this method are prohibited, infinite recursion is possible!

    if(!is_smart_contract(tr)) {
      return std::nullopt;
    }
    if(tr.user_field_ids().count(trx_uf::new_state::Value) > 0) {
      // new state contains unique field id
      const auto& smart_fld = tr.user_field(trx_uf::new_state::Value);
      if(smart_fld.is_valid()) {
        if(papi != nullptr) {
          bool present;
          const auto tmp = papi->getSmartContract(tr.source(), present); // tr.target() is also allowed
          if(present) {
            return tmp;
          }
        }
      }
    }
    // is executable:
    else {
      const auto& smart_fld = tr.user_field(trx_uf::deploy::Code); // trx_uf::start::Methods == trx_uf::deploy::Code
      if(smart_fld.is_valid()) {
        const auto invoke_info = deserialize<api::SmartContractInvocation>(smart_fld.value<std::string>());
        if(invoke_info.method.empty()) {
          // is deploy
          return invoke_info;
        }
        else {
          // is start
          // currently invoke_info contains all info required to execute contract, so
          // we need not acquire origin
          constexpr bool api_pass_code_and_methods = false;
          if constexpr(api_pass_code_and_methods)
          {
            return invoke_info;
          }
          // if api is refactored, we may need to acquire contract origin separately:
          if constexpr(!api_pass_code_and_methods)
          {
            bool present;
            if(papi != nullptr) {
              auto tmp = papi->getSmartContract(tr.target(), present);
              if(present) {
                tmp.method = invoke_info.method;
                tmp.params = invoke_info.params;
                return tmp;
              }
            }
          }
        }
      }
    }
    return std::nullopt;
  }

  //  const csdb::PoolHash blk_hash, cs::Sequence blk_seq, size_t trx_idx, cs::RoundNumber round
  void SmartContracts::enqueue(csdb::Pool block, size_t trx_idx)
  {
    if(trx_idx >= block.transactions_count()) {
      cserror() << name() << ": incorrect trx index in block to enqueue smart contract";
      return;
    }
    SmartContractRef new_item { block.hash(), block.sequence(), trx_idx };
    if(!exe_queue.empty()) {
      auto it = find_in_queue(new_item);
      // test duplicated contract call
      if(it != exe_queue.cend()) {
        cserror() << name() << ": attempt to queue duplicated contract transaction, already queued on round #" << it->round;
        return;
      }
    }
    // enqueue to end
    cslog() <<  "  _____";
    cslog() <<  " /     \\";
    cslog() <<  "/   +   \\";
    cslog() << "\\       /";
    cslog() << " \\_____/";
    csdb::Address addr = absolute_address(block.transaction(trx_idx).target());
    exe_queue.emplace_back(QueueItem { new_item, SmartContractStatus::Waiting, block.sequence(), addr, std::vector<csdb::Transaction>() });
    test_exe_queue();
  }

  void SmartContracts::on_new_state(csdb::Pool block, size_t trx_idx)
  {
    if(!block.is_valid() || trx_idx >= block.transactions_count()) {
      cserror() << name() << ": incorrect new_state transaction specfied";
    }
    else {
      auto new_state = get_transaction(SmartContractRef { block.hash(), block.sequence(), trx_idx });
      if(!new_state.is_valid()) {
        cserror() << name() << ": get new_state transaction failed";
      }
      else {
        csdb::UserField fld_contract_ref = new_state.user_field(trx_uf::new_state::RefStart);
        if(!fld_contract_ref.is_valid()) {
          cserror() << name() << ": new_state transaction does not contain reference to contract";
        }
        else {
          SmartContractRef contract_ref;
          contract_ref.from_user_field(fld_contract_ref);
          // update state
          csdb::UserField fld_state_value = new_state.user_field(trx_uf::new_state::Value);
          if(!fld_state_value.is_valid()) {
            cserror() << name() << ": contract new state does not contain state value";
          }
          else {
            std::string state_value = fld_state_value.value<std::string>();
            if(!state_value.empty()) {
              contract_state[absolute_address(new_state.target())] = state_value;
              csdebug() << name() << ": contract state updated, there are " << contract_state.size() << " items in states cache";
            }
            else {
              cswarning() << name() << ": contract state is not updated, new state is empty meaning execution is failed";
            }
          }
          remove_from_queue(contract_ref);
        }
      }
    }

    test_exe_queue();
  }

  void SmartContracts::test_exe_queue()
  {
    // select next queue item
    while(!exe_queue.empty()) {
      auto next = exe_queue.begin();
      if(next->status == SmartContractStatus::Finished) {
        csdebug() << name() << ": finished contract still in queue, remove it";
        remove_from_queue(exe_queue.cbegin());
        continue;
      }
      if(next->status == SmartContractStatus::Running) {
        // some contract is already running
        csdebug() << name() << ": there is running contract in queue";
        break;
      }
      csdebug() << name() << ": set running status to next contract in queue";
      next->status = SmartContractStatus::Running;
      if(!invoke_execution(next->contract)) {
        remove_from_queue(next);
      }
    }

    if(exe_queue.empty()) {
      csdebug() << name() << ": contract queue is empty, nothing to execute";
    }
  }

  bool SmartContracts::is_running_smart_contract(csdb::Address addr) const
  {
    if(!exe_queue.empty()) {
      const auto& current = exe_queue.front();
      if(current.status == SmartContractStatus::Running) {
        const csdb::Address tmp = absolute_address(addr);
        return (current.abs_addr == tmp);
      }
    }
    return false;
  }

  bool SmartContracts::test_smart_contract_emits(csdb::Transaction tr)
  {
    csdb::Address abs_addr = absolute_address(tr.source());

    if(is_running_smart_contract(abs_addr)) {

      // expect calls from api when trxs received
      std::lock_guard<std::mutex> lock(mtx_emit_transaction);

      auto& v = exe_queue.front().created_transactions;
      v.push_back(tr);
      csdebug() << name() << ": smart contract emits transaction, add, total " << v.size();
      return true;
    }
    else {
      if(contract_state.count(abs_addr)) {
        csdebug() << name() << ": inactive smart contract emits transaction, ignore";
      }
    }

    return false;
  }

  void SmartContracts::onStoreBlock(csdb::Pool block)
  {
    // control round-based timeout
    bool retest_required = false;
    for(auto& item : exe_queue) {
      if(item.status == SmartContractStatus::Running) {
        const auto seq = block.sequence();
        if(seq > item.round && seq - item.round > Consensus::MaxRoundsExecuteSmart) {
          cswarning() << name() << ": contract is in queue over " << Consensus::MaxRoundsExecuteSmart << " blocks, cancel it without transaction";
          item.status = SmartContractStatus::Finished;
          retest_required = true;
        }
      }
      else if(item.status == SmartContractStatus::Finished) {
        retest_required;
      }
    }
    if(retest_required) {
      test_exe_queue();
    }

    // inspect transactions against smart contracts, raise special event on every item found:
    if(block.transactions_count() > 0) {
      size_t tr_idx = 0;
      for(const auto& tr : block.transactions()) {
        if(is_smart_contract(tr)) {
          csdebug() << name() << ": smart contract trx #" << block.sequence() << "." << tr_idx;
          // dispatch transaction by its type
          bool is_deploy = this->is_deploy(tr);
          bool is_start = is_deploy ? false : this->is_start(tr);
          if(is_deploy || is_start) {
            if(is_deploy) {
              csdebug() << name() << ": smart contract is deployed, enqueue it for execution";
            }
            else {
              csdebug() << name() << ": smart contract is started, enqueue it for execution";
            }
            enqueue(block, tr_idx);
          }
          else if(is_new_state(tr)) {
            csdebug() << name() << ": smart contract state updated";
            on_new_state(block, tr_idx);
          }
        }
        ++tr_idx;
      }
    }
  }

  void SmartContracts::onReadBlock(csdb::Pool block, bool* should_stop)
  {
    //auto seq = block.sequence();
    //if(seq == 1925595 || seq == 1925395) {
    //  std::string state;
    //  ::general::Variant ret_val;
    //  if(block.transactions_count() > 0) {
    //    for(auto& tr : block.transactions()) {
    //      state = tr.user_field(-2).value<std::string>();
    //      ret_val = deserialize<::general::Variant>(tr.user_field(3).value<std::string>());
    //    }
    //  }
    //}


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
    cslog() <<  "  _____";
    cslog() <<  " /     \\";
    cslog() <<  "/   -   \\";
    cslog() << "\\       /";
    cslog() << " \\_____/";

    std::lock_guard<std::mutex> lock(mtx_emit_transaction);
    exe_queue.erase(it);
  }

  // returns false if failed, and caller must remove_from_queue() the item
  bool SmartContracts::invoke_execution(const SmartContractRef& contract)
  {
    csdb::Pool block = bc.loadBlock(contract.sequence);
    if(!block.is_valid()) {
      cserror() << name() << ": load block with smart contract failed, cancel execution";
      return false;
    }
    // call to executor only if currently is trusted
    if(force_execution || (execution_allowed && contains_me(block.confidants()))) {
      csdebug() << name() << ": execute current contract now";
      return execute(contract);
    }
    else {
      if(!execution_allowed) {
        csdebug() << name() << ": skip contract execution, it is disabled";
      }
      else {
        csdebug() << name() << ": skip contract execution, not in trusted list";
      }
    }
    // say do not remove item from queue:
    return true;
  }

  // returns false if execution canceled, so caller may call to remove_from_queue()
  bool SmartContracts::execute(const cs::SmartContractRef& item)
  {
    csdb::Transaction start_tr = get_transaction(item);
    if(!is_executable(start_tr)) {
      cserror() << name() << ": unable execute neither deploy nor start transaction";
      return false;
    }
    bool deploy = is_deploy(start_tr);

    csdebug() << name() << ": invoke api to remote executor to " << (deploy ? "deploy" : "execute") << " contract";

    auto maybe_contract = get_smart_contract(start_tr);
    if (maybe_contract.has_value()) {
      const auto contract = maybe_contract.value();
      std::string state;
      if (!deploy) {
        const auto it = contract_state.find(absolute_address(start_tr.target()));
        if (it != contract_state.cend()) {
          state = it->second;
        }
      }

      // create runnable object
      auto runnable = [=]() mutable {
        executor::ExecuteByteCodeResult resp;

        std::string error;
        try {
          if(contract.smartContractDeploy.byteCodeObjects.empty()) {
            return SmartExecutionData {};
          }
          get_api()->getExecutor().executeByteCode(resp, absolute_address(start_tr.source()).to_api_addr(), contract.smartContractDeploy.byteCodeObjects,
              state, contract.method, contract.params, Consensus::T_smart_contract);
        }
        catch(std::exception& x) {
          error = x.what();
        }
        catch(...) {
          error = " exception while executing byte code";
        }

        // locks until every emitted transactions by contract is placed to list
        std::lock_guard<std::mutex> lock(mtx_emit_transaction);
        if(error.empty()) {
          csdebug() << name() << ": smart contract call completed";
        }
        else {
          cserror() << name() << ": " << error;
        }
        return SmartExecutionData {
          start_tr,
          state,
          item,
          resp,
          error
        };
      };

      // run async and watch result
      auto watcher = cs::Concurrent::run(cs::RunPolicy::CallQueuePolicy, runnable);
      cs::Connector::connect(&watcher->finished, this, &SmartContracts::onExecutionFinished);

      executions_.push_back(std::move(watcher));

      csdb::Address addr = start_tr.target();
      return true;
    }
    else {
      cserror() << name() << ": failed get smart contract from transaction";
    }
    return false;
  }

  csdb::Transaction SmartContracts::result_from_smart_ref(const SmartContractRef& contract) const
  {
    csdb::Transaction src = get_transaction(contract);
    if(!src.is_valid()) {
      return csdb::Transaction {};
    }

    BlockChain::WalletData wallData {};
    BlockChain::WalletId wallId {};
    if(!bc.findWalletData(SmartContracts::absolute_address(src.target()), wallData, wallId)) {
      return csdb::Transaction {};
    }
  
    csdb::Transaction result(
      wallData.trxTail_.getLastTransactionId() + 1, // TODO: possible conflict with other innerIDs!
      src.target(), // contracts' key - source
      src.target(), // contracts' key - target
      src.currency(),
      0, // amount*/
      src.max_fee(), // TODO:: how to calculate max fee?
      src.counted_fee(), // TODO:: how to calculate fee?
      std::string {} //empty signature
    );
    // USRFLD1 - ref to start trx
    result.add_user_field(trx_uf::new_state::RefStart, contract.to_user_field());
    // USRFLD2 - total fee
    result.add_user_field(trx_uf::new_state::Fee, csdb::UserField(csdb::Amount(src.max_fee().to_double())));

    return result;
  }

  void SmartContracts::set_execution_result(cs::TransactionsPacket& pack) const
  {
    cslog() <<  "  _____";
    cslog() <<  " /     \\";
    cslog() <<  "/   =   \\";
    cslog() << "\\  " << std::setw(3) << pack.transactionsCount() << "  /";
    cslog() << " \\_____/";

    if(pack.transactionsCount() > 0) {
      const auto tr = pack.transactions().front();
      csdb::UserField f = tr.user_field(trx_uf::new_state::Value);
      if(f.is_valid()) {
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
  void SmartContracts::on_reject(cs::TransactionsPacket & pack)
  {
    if(exe_queue.empty()) {
      cserror() << name() << ": get rejected smart transactions but execution queue is empty";
      return;
    }

    // will contain "unchanged" states of rejected smart contract calls:
    cs::TransactionsPacket unchanged_pack;

    const auto cnt = pack.transactionsCount();
    if(cnt > 0) {
      csdebug() << name() << ": " << cnt << " trxs are rejected";
      std::vector<csdb::Address> done;
      for(const auto t : pack.transactions()) {
        csdb::Address abs_addr = absolute_address(t.source());
        if(std::find(done.cbegin(), done.cend(), abs_addr) != done.cend()) {
          continue;
        }

        // find source contract in queue
        for(auto it = exe_queue.cbegin(); it != exe_queue.cend(); ++it) {
          if(it->abs_addr == abs_addr) {
            done.push_back(abs_addr);
            csdebug() << name() << ": send to conveyer smart contract failed status (related trxs or state are rejected)";
            // send to consensus
            csdb::Transaction tr = result_from_smart_ref(it->contract);
            // result contains empty USRFLD[state::Value]
            tr.add_user_field(trx_uf::new_state::Value, std::string {});
            if(tr.is_valid()) {
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

        auto it = contract_state.find(absolute_address(t.source()));
      }

      if(unchanged_pack.transactionsCount() > 0) {
        Conveyer::instance().addSeparatePacket(unchanged_pack);
      }
    }
    else {
      cserror() << name() << ": trxs are rejected but list is empty";
    }
  }

} // cs
