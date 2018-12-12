#include <smartcontracts.hpp>
#include <solvercontext.hpp>

#include <lib/system/logger.hpp>
#include <csdb/currency.hpp>

#include <optional>
#include <sstream>

namespace cs
{

  class SmartContractsExecutor
  {
  public:

    static bool execute(SmartContracts& contracts, const cs::SmartContractRef& item);
  };

  /*static*/
  bool SmartContractsExecutor::execute(SmartContracts& contracts, const cs::SmartContractRef& item)
  {
    csdb::Transaction start_tr = contracts.get_transaction(item);
    //TODO: after debug completed remove the 2nd condidition:
    if(!SmartContracts::is_start(start_tr) && !SmartContracts::is_deploy(start_tr)) {
      cserror() << contracts.name() << ": unable execute non-start transaction";
      return false;
    }
    csdebug() << contracts.name() << ": imitate execution of start contract transaction";
    //DEBUG: currently, the start transaction contains result also
    csdb::Transaction result;
    result.set_innerID(start_tr.innerID() + 1); // TODO: possible conflict with spammer transactions!
    result.set_source(start_tr.target()); // contracts' key
    result.set_target(start_tr.target()); // contracts' key
    result.set_amount(0);
    result.set_max_fee(start_tr.max_fee());
    result.set_currency(start_tr.currency());
    // USRFLD0 - new state
    constexpr csdb::user_field_id_t smart_state_idx = ~1; // see apihandler.cpp #9
    const auto fields = start_tr.user_field_ids();
    if(fields.count(smart_state_idx) > 0) {
      result.add_user_field(trx_uf::new_state::Value, start_tr.user_field(smart_state_idx));
    }
    else {
      result.add_user_field(trx_uf::new_state::Value, csdb::UserField {});
    }
    // USRFLD1 - ref to start trx
    result.add_user_field(trx_uf::new_state::RefStart, item.to_user_field());
    // USRFLD2 - total fee
    result.add_user_field(trx_uf::new_state::Fee, csdb::UserField(csdb::Amount(start_tr.max_fee().to_double())));
    contracts.set_execution_result(result);
    return true;
  }

  /*explicit*/
  SmartContracts::SmartContracts(BlockChain& blockchain)
    : bc(blockchain)
  {
  }

  SmartContracts::~SmartContracts() = default;

  void SmartContracts::set_id(const cs::PublicKey& id)
  {
    node_id.resize(id.size());
    std::copy(id.cbegin(), id.cend(), node_id.begin());
  }

  /*static*/
  bool SmartContracts::is_smart_contract(const csdb::Transaction tr)
  {
    // see apihandler.cpp #319:
    //csdb::UserField uf = tr.user_field(0);
    //return uf.type() == csdb::UserField::Type::String;

    csdb::UserField f = tr.user_field(trx_uf::deploy::Code);
    return f.is_valid() && f.type() == csdb::UserField::Type::String;
  }

  /*static*/
  std::optional<api::SmartContractInvocation> SmartContracts::get_smart_contract(const csdb::Transaction tr)
  {
    const auto& smart_fld = tr.user_field(trx_uf::deploy::Code); // see apihandler.cpp near #494
    if(smart_fld.is_valid()) {
      return deserialize<api::SmartContractInvocation>(smart_fld.value<std::string>());
    }
    return std::nullopt;
  }

  /*static*/
  bool SmartContracts::is_deploy(const csdb::Transaction tr)
  {
    //TODO: correctly define tx type

    // see apihandler.cpp #319:
    if(!is_smart_contract(tr)) {
      return false;
    }
    auto contract = get_smart_contract(tr);
    if(!contract.has_value()) {
      return false;
    }
    return contract.value().method.empty();
  }

  /*static*/
  bool SmartContracts::is_start(const csdb::Transaction tr)
  {
    //TODO: correctly define tx type
    if(!is_smart_contract(tr)) {
      return false;
    }
    if(is_new_state(tr)) {
      return false;
    }
    if(is_deploy(tr)) {
      return false;
    }
    return true;
  }

  /*static*/
  bool SmartContracts::is_new_state(const csdb::Transaction tr)
  {
    //TODO: correctly define tx type
    if(!is_smart_contract(tr)) {
      return false;
    }
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
      csdebug() << name() << ": starting contract execution now";
      // call to executor only if currently is trusted
      if(contains_me(block.confidants())) {
        if(SmartContractsExecutor::execute(*this, new_item)) {
          return SmartContractStatus::Finished;
        }
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
      if(contains_me(block.confidants())) {
        csdebug() << name() << ": execute next contract in queue";
        SmartContractsExecutor::execute(*this, next->contract);
      }
    }
    else {
      csdebug() << name() << ": contract execution queue is empty";
    }
  }

} // cs
