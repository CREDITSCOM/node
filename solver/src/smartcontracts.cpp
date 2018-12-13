#include <smartcontracts.hpp>
#include <solvercontext.hpp>

#include <lib/system/logger.hpp>
#include <csdb/currency.hpp>
#include <ContractExecutor.h>

#include <optional>
#include <memory>

namespace cs
{

  class SmartContractsExecutor
  {
  public:

    static void init(Node::api_handler_ptr_t api)
    {
      papi = api;
    }

    static bool execute(SmartContracts& contracts, const cs::SmartContractRef& item);

  private:

    static Node::api_handler_ptr_t papi;
  };

  /*static*/
  Node::api_handler_ptr_t SmartContractsExecutor::papi;

  /*static*/
  bool SmartContractsExecutor::execute(SmartContracts& contracts, const cs::SmartContractRef& item)
  {
    csdb::Transaction start_tr = contracts.get_transaction(item);
    bool is_deploy = SmartContracts::is_deploy(start_tr);
    if(!is_deploy && !SmartContracts::is_start(start_tr)) {
      cserror() << contracts.name() << ": unable execute neither deploy nor start transaction";
      return false;
    }

    csdebug() << contracts.name() << ": invoke api to remote executor to execute contract";

    // partial return result init:
    csdb::Transaction result(
      start_tr.innerID() + 1, // TODO: possible conflict with innerIDs!
      start_tr.target(), // contracts' key
      start_tr.target(), // contracts' key
      start_tr.currency(),
      0, // amount*/
      start_tr.max_fee(), // TODO:: how to calculate max fee?
      start_tr.counted_fee(), // TODO:: how to calculate max fee?
      std::string {} //empty signature
    );
    // USRFLD1 - ref to start trx
    result.add_user_field(trx_uf::new_state::RefStart, item.to_user_field());
    // USRFLD2 - total fee
    result.add_user_field(trx_uf::new_state::Fee, csdb::UserField(csdb::Amount(start_tr.max_fee().to_double())));

    auto maybe_contract = contracts.get_smart_contract(start_tr);
    if(maybe_contract.has_value()) {
      const auto contract = maybe_contract.value();
      //APIHandler::smart_transaction_flow():
      executor::ExecuteByteCodeResult resp;
      std::string code;
      if(is_deploy) {
        code = contract.smartContractDeploy.byteCode;
      }
      else {
        //TODO: get contract code from deploy transaction
      }
      std::string state;
      constexpr const uint32_t MAX_EXECUTION_TIME = 1000;
      SmartContractsExecutor::papi->getExecutor().executeByteCode(resp, start_tr.source().to_api_addr(), code, state, contract.method, contract.params, MAX_EXECUTION_TIME);
      if(resp.status.code == 0) {
        // USRFLD0 - new state
        result.add_user_field(trx_uf::new_state::Value, resp.contractState);
        contracts.set_execution_result(result);
        return true;
      }
      else {
        cserror() << contracts.name() << ": failed to execute remotely smart contract";
      }
    }
    else {
      cserror() << contracts.name() << ": failed get smart contract from transaction";
    }

    csdebug() << contracts.name() << ": imitate execution of start contract transaction";

    // result does not contain USRFLD0 (contract state)
    contracts.set_execution_result(result);
    return true;
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

  void SmartContracts::init(const cs::PublicKey& id, Node::api_handler_ptr_t api)
  {
    node_id.resize(id.size());
    std::copy(id.cbegin(), id.cend(), node_id.begin());
    SmartContractsExecutor::init(api);
  }

  /*static*/
  bool SmartContracts::is_deploy(const csdb::Transaction tr)
  {
    if(!is_smart_contract(tr)) {
      return false;
    }
    // deploy -> 1 user field, start -> 2 user fields, new_state -> 3 user fields
    return (tr.user_field_ids().size() == trx_uf::deploy::Count);
  }

  /*static*/
  bool SmartContracts::is_start(const csdb::Transaction tr)
  {
    if(!is_smart_contract(tr)) {
      return false;
    }
    // deploy -> 1 user field, start -> 2 user fields, new_state -> 3 user fields
    return tr.user_field_ids().size() == trx_uf::start::Count;
  }

  /*static*/
  bool SmartContracts::is_new_state(const csdb::Transaction tr)
  {
    if(!is_smart_contract(tr)) {
      return false;
    }
    // deploy -> 1 user field, start -> 2 user fields, new_state -> 3 user fields
    return tr.user_field_ids().size() == trx_uf::new_state::Count;
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

  bool SmartContracts::is_smart_contract(const csdb::Transaction tr)
  {
    // see apihandler.cpp #319:
    //csdb::UserField uf = tr.user_field(0);
    //return uf.type() == csdb::UserField::Type::String;

    if(!tr.is_valid()) {
      return false;
    }
    csdb::UserField f = tr.user_field(trx_uf::deploy::Code);
    return f.is_valid() && f.type() == csdb::UserField::Type::String;
  }

  std::optional<api::SmartContractInvocation> SmartContracts::get_smart_contract(const csdb::Transaction tr)
  {
    if(SmartContracts::is_deploy(tr) || SmartContracts::is_new_state(tr)) {
      const auto& smart_fld = tr.user_field(trx_uf::deploy::Code);
      if(smart_fld.is_valid()) {
        return deserialize<api::SmartContractInvocation>(smart_fld.value<std::string>());
      }
    }
    else if(SmartContracts::is_start(tr)) {
      const auto& ref_fld = tr.user_field(trx_uf::start::RefState);
      if(ref_fld.is_valid()) {
        SmartContractRef contract;
        contract.from_user_field(ref_fld);
        // must be new_state transaction
        const auto ref_tr = get_transaction(contract);
        if(!SmartContracts::is_new_state(ref_tr) && !SmartContracts::is_deploy(tr)) {
          cserror() << "Smarts: start transaction does not refer to new_state/deploy one";
          return std::nullopt;
        }
        // single-level recursion:
        return get_smart_contract(ref_tr);
      }
    }
    return std::nullopt;
  }

  //  const csdb::PoolHash blk_hash, csdb::Pool::sequence_t blk_seq, size_t trx_idx, cs::RoundNumber round
  SmartContractStatus SmartContracts::enqueue(csdb::Pool block, size_t trx_idx)
  {
    SmartContractRef new_item { block.hash(), block.sequence(), trx_idx };
    SmartContractStatus new_status = SmartContractStatus::Running;
    if(!exe_queue.empty()) {
      auto it = find_in_queue(new_item);
      // test the same contract
      if(it != exe_queue.cend()) {
        cserror() << name() << ": attempt to queue duplicated contract transaction, already queued on round #" << it->round;
        return it->status;
      }
      // only the 1st item currently is allowed to execute
      new_status = SmartContractStatus::Waiting;
      csdebug() << name() << ": enqueue contract for future execution";
    }
    else {
      if(invoke_execution(new_item, block)) {
        return SmartContractStatus::Finished;
      }
      // wait until executed, place new_item to queue
      //new_status = SmartContractStatus::Running;
    }
    // enqueue to end
    exe_queue.emplace_back(QueueItem { new_item, new_status, static_cast<cs::RoundNumber>(block.sequence()) });
    return new_status;
  }

  void SmartContracts::on_completed(csdb::Pool block, size_t trx_idx)
  {
    auto it = find_in_queue(SmartContractRef { block.hash(), block.sequence(), trx_idx });
    if(it == exe_queue.cend()) {
      cserror() << name() << ": completed contract is not in queue";
      return;
    }
    if(it != exe_queue.cbegin()) {
      cswarning() << name() << ": completed contract is not at the top of queue";
    }
    exe_queue.erase(it);
    if(!exe_queue.empty()) {
      csdebug() << name() << ": set running status to next contract in queue";
      auto next = exe_queue.begin();
      next->status = SmartContractStatus::Running;
      invoke_execution(next->contract, block);
    }
    else {
      csdebug() << name() << ": contract execution queue is empty";
    }
  }

  bool SmartContracts::invoke_execution(const SmartContractRef& contract, csdb::Pool block)
  {
    // call to executor only if currently is trusted
    if(force_execution || (execution_allowed && contains_me(block.confidants()))) {
      csdebug() << name() << ": execute current contract in queue now";
      SmartContractsExecutor::execute(*this, contract);
      return true;
    }
    else {
      if(!execution_allowed) {
        csdebug() << name() << ": skip contract execution, it is disabled";
      }
      else {
        csdebug() << name() << ": skip contract execution, not in trusted list";
      }
    }
    return false;
  }

} // cs
