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

  class SmartContractsExecutor
  {
  public:

    static bool execute(SmartContracts& contracts, const cs::SmartContractRef& item);
  };

  /*static*/
  bool SmartContractsExecutor::execute(SmartContracts& contracts, const cs::SmartContractRef& item)
  {
    csdb::Transaction start_tr = contracts.get_transaction(item);
    bool is_deploy = contracts.is_deploy(start_tr);
    if(!is_deploy && !contracts.is_start(start_tr)) {
      cserror() << contracts.name() << ": unable execute neither deploy nor start transaction";
      return false;
    }

    csdebug() << contracts.name() << ": invoke api to remote executor to execute contract";

    // partial return result init:
    csdb::Transaction result(
      start_tr.innerID() + 1, // TODO: possible conflict with innerIDs!
      start_tr.target(), // contracts' key - source
      start_tr.target(), // contracts' key - target
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
      contracts.get_api()->getExecutor().executeByteCode(resp, start_tr.source().to_api_addr(), code, state, contract.method, contract.params, MAX_EXECUTION_TIME);
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

  bool SmartContracts::is_deploy(const csdb::Transaction tr) const
  {
    if(!is_smart_contract(tr)) {
      return false;
    }
    if(is_new_state(tr)) {
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
    if(!is_smart_contract(tr)) {
      return false;
    }
    if(is_new_state(tr)) {
      return false;
    }
    const auto invoke = get_smart_contract(tr);
    if(!invoke.has_value()) {
      return false;
    }
    // deploy ~ start but method in invoke info is empty
    return ! invoke.value().method.empty();
  }

  /* static */
  /* Assuming deployer.is_public_key() */
  csdb::Address SmartContracts::get_valid_smart_address(const csdb::Address& deployer,
                                                        const uint64_t trId,
                                                        const api::SmartContractDeploy& data) {
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

  /*static*/
  bool SmartContracts::is_smart_contract(const csdb::Transaction tr)
  {
    // see apihandler.cpp #319:
    //csdb::UserField uf = tr.user_field(0);
    //return uf.type() == csdb::UserField::Type::String;

    if(!tr.is_valid()) {
      return false;
    }
    // to contain smart contract trx must contain either FLD"0" (deploy, start) or FLD"-2" (new_state):
    csdb::UserField f = tr.user_field(trx_uf::deploy::Code);
    if (!f.is_valid()) {
      f = tr.user_field(trx_uf::new_state::Value);
    }
    return f.is_valid() && f.type() == csdb::UserField::Type::String;
  }

  std::optional<api::SmartContractInvocation> SmartContracts::get_smart_contract(const csdb::Transaction tr) const
  {
    if(SmartContracts::is_deploy(tr)) {
      const auto& smart_fld = tr.user_field(trx_uf::deploy::Code);
      if(smart_fld.is_valid()) {
        return deserialize<api::SmartContractInvocation>(smart_fld.value<std::string>());
      }
    }
    else if(SmartContracts::is_new_state(tr)) {
      const auto& smart_fld = tr.user_field(trx_uf::new_state::Value);
      if(smart_fld.is_valid()) {
        bool present;
        const auto tmp = papi->getSmartContract(tr.source(), present);
        if(present) {
          return tmp;
        }
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

  //  const csdb::PoolHash blk_hash, cs::Sequence blk_seq, size_t trx_idx, cs::RoundNumber round
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
    exe_queue.emplace_back(QueueItem { new_item, new_status, block.sequence() });
    return new_status;
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
    auto it = find_in_queue(contract_ref);
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
