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
  SmartContracts::SmartContracts(BlockChain& blockchain)
    : bc(blockchain)
    , execution_allowed(true)
    , force_execution(false)
  {
#if defined(DEBUG_SMARTS)
    execution_allowed = false;
#endif
  }

  SmartContracts::~SmartContracts() = default;

  void SmartContracts::init(const cs::PublicKey& id, csconnector::connector::ApiHandlerPtr api)
  {
    papi = api;
    node_id.resize(id.size());
    std::copy(id.cbegin(), id.cend(), node_id.begin());
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

    const auto invoke = get_smart_contract(tr);
    if(!invoke.has_value()) {
      return false;
    }
    // deploy ~ start but method in invoke info is empty
    return invoke.value().method.empty();
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
    strToHash.reserve(cscrypto::kPublicKeySize + 6 + data.byteCode.size());

    const auto dPk = deployer.public_key();
    const auto idPtr = reinterpret_cast<const cscrypto::Byte*>(&trId);

    std::copy(dPk.begin(), dPk.end(), std::back_inserter(strToHash));
    std::copy(idPtr, idPtr + 6, std::back_inserter(strToHash));
    std::copy(data.byteCode.begin(),
      data.byteCode.end(),
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
    csdb::Pool block = storage.loadBlock(contract.hash);
    if(!block.is_valid()) {
      return csdb::Transaction {};
    }
    if(contract.transaction >= block.transactions_count()) {
      return csdb::Transaction {};
    }
    return block.transactions().at(contract.transaction);
  }

  void SmartContracts::onExecutionFinished(const SmartExecutionData& data) {
    csdb::Transaction result = result_from_smart_invoke(data.smartContract);
    const QueueItem* pqueue_item = nullptr;
    const auto it = find_in_queue(data.smartContract);
    if(it != exe_queue.cend()) {
      pqueue_item = &(*it);
    }
    else {
      cserror() << name() << ": cannot find in queue just completed contract";
    }

    if (data.result.status.code == 0) {
      csdebug() << name() << ": execution of smart contract is successful";
      result.add_user_field(trx_uf::new_state::Value, data.result.contractState);
    }
    else {
      cserror() << name() << ": failed to execute smart contract";
      if(pqueue_item != nullptr) {
        if(!pqueue_item->created_transactions.empty()) {
          cswarning() << name() << ": drop " << pqueue_item->created_transactions.size() << " emitted trx";
        }
        // ignore emitted transactions even if any:
        pqueue_item = nullptr;
      }
      // result contains empty USRFLD[state::Value]
      result.add_user_field(trx_uf::new_state::Value, std::string {});
    }

    cs::TransactionsPacket packet;
    packet.addTransaction(result);
    if(data.result.status.code == 0) {
      if(!pqueue_item->created_transactions.empty()) {
        for(const auto& tr : pqueue_item->created_transactions) {
          packet.addTransaction(tr);
        }
        cslog() << name() << ": add " << pqueue_item->created_transactions.size() << " emitted trx to contract state";
      }
      else {
        cslog() << name() << ": no emitted trx added to contract state";
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
        bool present;
        const auto tmp = papi->getSmartContract(tr.source(), present); // tr.target() is also allowed
        if(present) {
          return tmp;
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
          constexpr bool api_pass_code_and_methods = true;
          if constexpr(api_pass_code_and_methods)
          {
            return invoke_info;
          }
          // if api is refactored, we may need to acquire contract origin separately:
          if constexpr(!api_pass_code_and_methods)
          {
            bool present;
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
    return std::nullopt;
  }

  //  const csdb::PoolHash blk_hash, cs::Sequence blk_seq, size_t trx_idx, cs::RoundNumber round
  void SmartContracts::enqueue(csdb::Pool block, size_t trx_idx)
  {
    SmartContractRef new_item { block.hash(), block.sequence(), trx_idx };
    if(!exe_queue.empty()) {
      auto it = find_in_queue(new_item);
      // test the same contract
      if(it != exe_queue.cend()) {
        cserror() << name() << ": attempt to queue duplicated contract transaction, already queued on round #" << it->round;
        return;
      }
    }
    // enqueue to end
    cslog() << "  _____";
    cslog() << " /     \\";
    cslog() << "/  S.C  \\";
    cslog() << "\\   .   /";
    cslog() << " \\_____/";
    csdb::Address addr {};
    if(trx_idx < block.transactions_count()) {
      addr = absolute_address(block.transaction(trx_idx).target());
    }
    exe_queue.emplace_back(QueueItem { new_item, SmartContractStatus::Waiting, block.sequence(), addr, std::vector<csdb::Transaction>() });
    test_exe_queue();
  }

  void SmartContracts::on_completed(csdb::Pool block, size_t trx_idx)
  {
    if(!block.is_valid() || trx_idx >= block.transactions_count()) {
      cserror() << name() << ": incorrect new_state transaction specfied";
      return;
    }
    auto new_state = get_transaction(SmartContractRef { block.hash(), block.sequence(), trx_idx });
    if(!new_state.is_valid()) {
      cserror() << name() << ": get new_state transaction failed";
      return;
    }
    csdb::UserField fld_contract_ref = new_state.user_field(trx_uf::new_state::RefStart);
    if(!fld_contract_ref.is_valid()) {
      cserror() << name() << ": new_state transaction does not contain reference to contract";
      return;
    }
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
    test_exe_queue();
  }

  void SmartContracts::test_exe_queue()
  {
    // select next queue item
    while(!exe_queue.empty()) {
      auto next = exe_queue.begin();
      if(next->status == SmartContractStatus::Running) {
        // some contract is already running
        break;
      }
      csdebug() << name() << ": set running status to next contract in queue";
      next->status = SmartContractStatus::Running;
      if(!invoke_execution(next->contract)) {
        exe_queue.erase(next);
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
      cslog() << name() << ": running smart contract emits transaction, add, total " << v.size();
      return true;
    }
    else {
      if(contract_state.count(abs_addr)) {
        cslog() << name() << ": inactive smart contract emits transaction, ignore";
      }
    }

    return false;
  }

  void SmartContracts::remove_from_queue(const SmartContractRef & item)
  {
    auto it = find_in_queue(item);
    if(it == exe_queue.cend()) {
      cserror() << name() << ": contract to remove is not in queue";
      return;
    }
    if(it != exe_queue.cbegin()) {
      cswarning() << name() << ": completed contract is not at the top of queue";
    }
    cslog() << "  _____";
    cslog() << " /     \\";
    cslog() << "/   .   \\";
    cslog() << "\\  S.C  /";
    cslog() << " \\_____/";
    exe_queue.erase(it);
  }

  void SmartContracts::cancel_running_smart_contract()
  {
    if(!exe_queue.empty()) {
      const auto& current = exe_queue.front();
      if(current.status == SmartContractStatus::Running) {
        csdb::Transaction tr = result_from_smart_invoke(current.contract);
        if(tr.is_valid()) {
          cswarning() << name() << ": cancel execution of smart contract";
          // result contains empty USRFLD[state::Value]
          tr.add_user_field(trx_uf::new_state::Value, std::string {});
          cs::TransactionsPacket packet;
          packet.addTransaction(tr);
          set_execution_result(packet);
        }
        else {
          cserror() << name() << ": failed to cancel execution of smart contract";
        }
        exe_queue.erase(exe_queue.cbegin());
      }
    }
    test_exe_queue();
  }

  // returns false if caller must call to remove_from_queue()
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

      constexpr const uint32_t MAX_EXECUTION_TIME = 1000;

      // create runnable object
      auto runnable = [=]() mutable {
        executor::ExecuteByteCodeResult resp;
        get_api()->getExecutor().executeByteCode(resp, start_tr.source().to_api_addr(), contract.smartContractDeploy.byteCode,
                                                 state, contract.method, contract.params, MAX_EXECUTION_TIME);

        std::lock_guard<std::mutex> lock(mtx_emit_transaction);

        csdebug() << name() << ": smart contract call completed";

        SmartExecutionData data = {
          start_tr,
          state,
          item,
          resp
        };

        return data;
      };

      // run async and watch result
      auto watcher = cs::Concurrent::run(cs::RunPolicy::CallQueuePolicy, runnable);
      cs::Connector::connect(watcher->finished, this, &SmartContracts::onExecutionFinished);

      executions_.push_back(std::move(watcher));

      csdb::Address addr = start_tr.target();
      auto cleanupOnTimeout = [=]() {
        if (is_running_smart_contract(addr)) {
          cswarning() << name() << ": timeout of smart contract execution, cancel";
          cancel_running_smart_contract();
        }
        else {
          cslog() << name() << ": unexpected smart contract address, cannot cancel execution";
        }
      };

      cs::Timer::singleShot(static_cast<int>(MAX_EXECUTION_TIME << 1), cleanupOnTimeout);
      return true;
    }
    else {
      cserror() << name() << ": failed get smart contract from transaction";
    }
    return false;
  }

  csdb::Transaction SmartContracts::result_from_smart_invoke(const SmartContractRef& contract) const
  {
    csdb::Transaction src = get_transaction(contract);
    if(!src.is_valid()) {
      return csdb::Transaction {};
    }
    csdb::Transaction result(
      src.innerID() + 1, // TODO: possible conflict with other innerIDs!
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

  void SmartContracts::set_execution_result(cs::TransactionsPacket pack) const
  {
    emit signal_smart_executed(pack);

    cslog() << "  _____";
    cslog() << " /     \\";
    cslog() << "/  S.C  \\";

    bool ok = true;
    if(pack.transactionsCount() > 0) {
      for(const auto& tr : pack.transactions()) {
        if(is_new_state(tr)) {
          if(tr.user_field(trx_uf::new_state::Value).value<std::string>().empty()) {
            ok = false;
            break;
          }
        }
      }
    }
    if(ok) {
      cslog() << "\\   +   /";
    }
    else {
      cslog() << "\\ ERROR /";
    }
    cslog() << " \\_____/";

  }

} // cs
