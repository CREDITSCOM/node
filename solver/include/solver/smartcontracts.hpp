#pragma once

#include <csdb/address.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <csdb/user_field.hpp>
#include <lib/system/common.hpp>
#include <lib/system/concurrent.hpp>
#include <lib/system/signals.hpp>
#include <lib/system/logger.hpp>

#include <csnode/node.hpp>  // introduce csconnector::connector::ApiExecHandlerPtr as well

#include <list>
#include <mutex>
#include <optional>
#include <vector>

//#define DEBUG_SMARTS

class BlockChain;
class CallsQueueScheduler;

namespace csdb {
class Transaction;
}

namespace cs {

  // smart contract related error codes
  namespace error
  {
    // timeout during operation
    constexpr uint8_t TimeExpired = 254;
    // insufficient funds to complete operation
    constexpr uint8_t OutOfFunds = 253;
    // std::exception thrown
    constexpr uint8_t StdException = 252;
    // other exception thrown
    constexpr uint8_t Exception = 251;
    // replenished contract does not implement payable()
    constexpr uint8_t UnpayableReplenish = 250;
    // the trusted consensus have rejected new_state (and emitted transactions)
    constexpr uint8_t ConsensusRejected = 249;
    // error in Executor::ExecuteTransaction()
    constexpr uint8_t ExecuteTransaction = 248;
    // bug in SmartContracts
    constexpr uint8_t InternalBug = 247;
    // executor is disconnected or unavailable, value is hard-coded in ApiExec module
    constexpr uint8_t NoExecutor = 1;
  }

  // transactions user fields
  namespace trx_uf
  {
    // deploy transaction fields
    namespace deploy
    {
      // byte-code (string)
      constexpr csdb::user_field_id_t Code = 0;
      // executed contract may perform subsequent contracts call,
      // serialized into string: count (byte) + count * contract absolute address
      constexpr csdb::user_field_id_t Using = 1;
      // count of user fields
      constexpr size_t Count = 2;
    }  // namespace deploy
    // start transaction fields
    namespace start
    {
      // methods with args (string)
      constexpr csdb::user_field_id_t Methods = 0;
      // reference to last state transaction
      constexpr csdb::user_field_id_t RefState = 1;
      // executed contract may perform subsequent contracts call,
      // serialized into string: count (byte) + count * contract absolute address
      constexpr csdb::user_field_id_t Using = 2;
      // count of user fields, may vary from 1 (source is person) to 2 (source is another contract)
      // constexpr size_t Count = {1,2};
    }  // namespace start
    // new state transaction fields
    namespace new_state
    {
      // new state value, new byte-code (string)
      constexpr csdb::user_field_id_t Value = ~1;  // see apihandler.cpp #9 for currently used value ~1
      // reference to start transaction
      constexpr csdb::user_field_id_t RefStart = 1;
      // fee value
      constexpr csdb::user_field_id_t Fee = 2;
      // return value
      constexpr csdb::user_field_id_t RetVal = 3;
      // count of user fields
      constexpr size_t Count = 4;
    }  // namespace new_state
    // smart-gen transaction field
    namespace smart_gen
    {
      // reference to start transaction
      constexpr csdb::user_field_id_t RefStart = 0;
    }  // namespace smart_gen
    // ordinary transaction field
    namespace ordinary
    {
      // no fields defined
    }
  }  // namespace trx_uf

struct SmartContractRef {
  // block hash
  csdb::PoolHash hash;
  // block sequence
  cs::Sequence sequence;
  // transaction sequence in block, instead of ID
  size_t transaction;

  SmartContractRef()
    : sequence(std::numeric_limits<decltype(sequence)>().max())
    , transaction(std::numeric_limits<decltype(sequence)>().max())
  {}

  SmartContractRef(const csdb::PoolHash block_hash, cs::Sequence block_sequence, size_t transaction_index)
    : hash(block_hash)
    , sequence(block_sequence)
    , transaction(transaction_index)
  {}

  SmartContractRef(const csdb::UserField& user_field)
  {
    from_user_field(user_field);
  }

  bool is_valid() const
  {
    if(hash.is_empty()) {
      return false;
    }
    return (sequence != std::numeric_limits<decltype(sequence)>().max() &&
      transaction != std::numeric_limits<decltype(sequence)>().max() &&
      !hash.is_empty());
  }

  // "serialization" methods

  csdb::UserField to_user_field() const;
  void from_user_field(const csdb::UserField& fld);

  csdb::TransactionID getTransactionID() const {
    return csdb::TransactionID(hash, transaction);
  }
};

struct SmartExecutionData {
  SmartContractRef contract_ref;
  csdb::Amount executor_fee;
  executor::Executor::ExecuteResult result;
  std::string error;
};

inline bool operator==(const SmartContractRef& l, const SmartContractRef& r) {
  return (l.transaction == r.transaction && l.sequence == r.sequence /*&& l.hash == r.hash*/);
}

inline bool operator<(const SmartContractRef& l, const SmartContractRef& r) {
  if (l.sequence < r.sequence) {
    return true;
  }
  if (l.sequence > r.sequence) {
    return false;
  }
  return (l.transaction < r.transaction);
}

enum class SmartContractStatus {
  // contract is not involved in any way
  Idle,
  // is waiting until execution starts
  Waiting,
  // is executing at the moment, is able to emit transactions
  Running,
  // execution is finished, waiting for new state transaction in blockchain, no more transaction emitting is allowed
  Finished,
  // contract is closed, neither new_state nor emitting transactions are allowed, should be removed from queue
  Closed
};

// to inform subscribed slots on deploy/execute/replenish occur
// passes to every slot packet with result transactions
using SmartContractExecutedSignal = cs::Signal<void(cs::TransactionsPacket)>;

// to inform subscribed slots on deploy/execution/replenish completion or timeout
// passes to every slot the "starter" transaction
using SmartContractSignal = cs::Signal<void(csdb::Transaction)>;

class SmartContracts final {
public:
  explicit SmartContracts(BlockChain&, CallsQueueScheduler&);

  SmartContracts() = delete;
  SmartContracts(const SmartContracts&) = delete;

  ~SmartContracts();

  void init(const cs::PublicKey&, Node* node);

  // test transaction methods

  // smart contract related transaction of any type
  static bool is_smart_contract(const csdb::Transaction&);
  // deploy or start contract
  static bool is_executable(const csdb::Transaction& tr);
  // deploy contract
  static bool is_deploy(const csdb::Transaction&);
  // start contract
  static bool is_start(const csdb::Transaction&);
  // new state of contract, result of invocation of executable transaction
  static bool is_new_state(const csdb::Transaction&);

  /* Assuming deployer.is_public_key(), not a WalletId */
  static csdb::Address get_valid_smart_address(const csdb::Address& deployer, const uint64_t trId,
                                               const api::SmartContractDeploy&);

  // to/from user filed serializations
  
  static bool uses_contracts(const csdb::Transaction& tr);
  static std::vector<csdb::Address> get_using_contracts(const csdb::UserField& fld);
  static csdb::UserField set_using_contracts(const std::vector<csdb::Address>& using_list);

  std::optional<api::SmartContractInvocation> get_smart_contract(const csdb::Transaction& tr)
  {
    cs::Lock lock(public_access_lock);
    return std::move(get_smart_contract_impl(tr));
  }

  // get & handle rejected transactions
  // usually ordinary consensus may reject smart-related transactions
  void on_reject(cs::TransactionsPacket& pack);

  csdb::Address absolute_address(const csdb::Address& optimized_address) const {
    return bc.get_addr_by_type(optimized_address, BlockChain::ADDR_TYPE::PUBLIC_KEY);
  }

  bool is_closed_smart_contract(const csdb::Address& addr) const
  {
    cs::Lock lock(public_access_lock);
    return get_smart_contract_status(addr) == SmartContractStatus::Closed;
  }

  bool is_known_smart_contract(const csdb::Address& addr) const {
    cs::Lock lock(public_access_lock);
    return in_known_contracts(addr);
  }

  bool is_contract_locked(const csdb::Address& addr) const {
    cs::Lock lock(public_access_lock);
    return is_locked(absolute_address(addr));
  }

  // return true if SmartContracts provide special handling for transaction, so
  // the transaction is not pass through conveyer
  // method is thread-safe to be called from API thread
  bool capture_transaction(const csdb::Transaction& t);

public signals:
  SmartContractExecutedSignal signal_smart_executed;
  SmartContractSignal signal_payable_invoke;
  SmartContractSignal signal_payable_timeout;

public slots:
  // called when execute_async() completed
  void on_execution_completed(const SmartExecutionData& data)
  {
    cs::Lock lock(public_access_lock);
    on_execution_completed_impl(data);
  }

  // called when next block is stored
  void on_store_block(const csdb::Pool& block);

  // called when next block is read from database
  void on_read_block(const csdb::Pool& block, bool* should_stop);

private:
  using trx_innerid_t = int64_t;  // see csdb/transaction.hpp near #101

  const char *PayableName = "payable";
  const char *PayableRetType = "void";
  const char *PayableArgType = "java.lang.String";
  const char *PayableNameArg0 = "amount";
  const char *PayableNameArg1 = "currency";

  const char *UsesContract = "Contract";
  const char *UsesContractAddr = "address";
  const char *UsesContractMethod = "method";

  BlockChain& bc;
  CallsQueueScheduler& scheduler;
  cs::PublicKey node_id;
  // be careful, may be equal to nullptr if api is not initialized (for instance, blockchain failed to load)
  csconnector::connector::ApiExecHandlerPtr exec_handler_ptr;
 
  // flag to allow execution, currently depends on executor presence
  bool execution_allowed;

  CallsQueueScheduler::CallTag tag_cancel_running_contract;

  enum class PayableStatus : int {
    Unknown = -1,
    Absent = 0,
    Implemented = 1
  };

  struct StateItem {
    // is temporary locked from execution until current execution completed
    bool is_locked{ false };
    // payable() method is implemented
    PayableStatus payable{ PayableStatus::Unknown };
    // reference to deploy transaction
    SmartContractRef ref_deploy;
    // reference to last successful execution which state is stored by item, may be equal to ref_deploy
    SmartContractRef ref_execute;
    // current state which is result of last successful execution / deploy
    std::string state;
    // using other contracts: [own_method] - [ [other_contract - its_method], ... ], ...
    std::map<std::string, std::map<csdb::Address, std::string>> uses;
  };

  // last contract's state storage
  std::map<csdb::Address, StateItem> known_contracts;

  // contract replenish transactions stored during reading from DB on stratup
  std::vector<SmartContractRef> replenish_contract;

  // async watchers
  std::list<cs::FutureWatcherPtr<SmartExecutionData>> executions_;

  struct QueueItem {
    // reference to smart in block chain (block/transaction) that spawns execution
    SmartContractRef ref_start;
    // current status (running/waiting)
    SmartContractStatus status;
    // enqueue round
    cs::Sequence seq_enqueue;
    // start round
    cs::Sequence seq_start;
    // finish round
    cs::Sequence seq_finish;
    // smart contract wallet/pub.key absolute address
    csdb::Address abs_addr;
    // max fee taken from contract starter transaction
    csdb::Amount avail_fee;
    // new_state fee prediction
    csdb::Amount new_state_fee;
    // current fee
    csdb::Amount consumed_fee;
    // actively taking part in smart consensus, perform a call to executor
    bool is_executor;
    // actual consensus
    std::unique_ptr<SmartConsensus> pconsensus;
    // using contracts, must store absolute addresses (public keys)
    std::vector<csdb::Address> uses;

    QueueItem(const SmartContractRef& ref_contract, csdb::Address absolute_address, csdb::Transaction tr_start)
      : ref_start(ref_contract)
      , status(SmartContractStatus::Waiting)
      , seq_enqueue(0)
      , seq_start(0)
      , seq_finish(0)
      , abs_addr(absolute_address)
      , consumed_fee(0)
      , is_executor(false)
    {
      avail_fee = csdb::Amount(tr_start.max_fee().to_double());
      csdb::Amount tr_start_fee = csdb::Amount(tr_start.counted_fee().to_double());

      // apply starter fee consumed
      avail_fee -= tr_start_fee;

      //TODO: here new_state_fee prediction may be calculated, currently it is equal to starter fee
      new_state_fee = tr_start_fee;

      csdb::UserField fld_using{};
      if (SmartContracts::is_start(tr_start)) {
        fld_using = tr_start.user_field(trx_uf::start::Using);
      }
      else if (SmartContracts::is_deploy(tr_start)) {
        fld_using = tr_start.user_field(trx_uf::deploy::Using);
      }

      // apply reserved new_state fee
      avail_fee -= new_state_fee;

      // get using contracts and reserve more fee for new_states
      if (fld_using.is_valid()) {
        uses = std::move(SmartContracts::get_using_contracts(fld_using));
        // reserve new_state fee for every using contract also
        for (const auto& it : uses) {
          csunused(it);
          avail_fee -= new_state_fee;
        }
      }
    }
  };

  // executiom queue
  std::vector<QueueItem> exe_queue;

  // is locked in all non-static public methods
  // is locked in const methods also
  mutable cs::SpinLock public_access_lock;

  using queue_iterator = std::vector<QueueItem>::iterator;
  using queue_const_iterator = std::vector<QueueItem>::const_iterator;

  Node* pnode;

  queue_iterator find_in_queue(const SmartContractRef& item)
  {
    auto it = exe_queue.begin();
    for (; it != exe_queue.end(); ++it) {
      if (it->ref_start == item) {
        break;
      }
    }
    return it;
  }

  queue_iterator find_first_in_queue(const csdb::Address& abs_addr)
  {
    auto it = exe_queue.begin();
    for(; it != exe_queue.end(); ++it) {
      if(it->abs_addr == abs_addr) {
        break;
      }
    }
    return it;
  }

  queue_const_iterator find_first_in_queue(const csdb::Address& abs_addr) const
  {
    auto it = exe_queue.begin();
    for(; it != exe_queue.end(); ++it) {
      if(it->abs_addr == abs_addr) {
        break;
      }
    }
    return it;
  }

  // return next element in queue, the only exception is end() which returns unmodified
  queue_iterator remove_from_queue(queue_iterator it);

  void remove_from_queue(const SmartContractRef& item) {
    remove_from_queue(find_in_queue(item));
  }

  SmartContractStatus get_smart_contract_status(const csdb::Address& addr) const;

  void checkAllExecutions();

  void test_exe_queue();

  // true if target of transaction is smart contract which implements payable() method
  bool is_payable_target(const csdb::Transaction& tr);

  // true if transaction replenishes balance of smart contract
  bool is_replenish_contract(const csdb::Transaction& tr);

  // tests passed list of trusted nodes to contain own node
  bool contains_me(const std::vector<cs::PublicKey>& list) const {
    return (list.cend() != std::find(list.cbegin(), list.cend(), node_id));
  }

  static csdb::Transaction get_transaction(BlockChain& storage, const SmartContractRef& contract);

  // non-static variant
  csdb::Transaction get_transaction(const SmartContractRef& contract) const {
    return SmartContracts::get_transaction(bc, contract);
  }

  void enqueue(const csdb::Pool& block, size_t trx_idx);

  void on_new_state(const csdb::Pool& block, size_t trx_idx);

  // perform async execution via API to remote executor
  // returns false if execution is canceled
  bool execute_async(const cs::SmartContractRef& item, csdb::Amount avail_fee);

  // makes a transaction to store new_state of smart contract invoked by src
  // caller is responsible to test src is a smart-contract-invoke transaction
  csdb::Transaction create_new_state(const QueueItem& queue_item) const;

  // update in contracts table appropriate item's state
  bool update_contract_state(const csdb::Transaction& t);

  // get deploy info from cached deploy transaction reference
  std::optional<api::SmartContractInvocation> find_deploy_info(const csdb::Address& abs_addr) const;

  // test if abs_addr is address of smart contract with payable() implemented;
  // may make a BLOCKING call to java executor
  bool is_payable(const csdb::Address& abs_addr);

  // test if metadata is actualized for given contract
  // may make a BLOCKING call to java executor
  bool is_metadata_actual(const csdb::Address& abs_addr)
  {
    const auto it = known_contracts.find(abs_addr);
    if (it != known_contracts.cend()) {
      // both uses list and defined payable means metadata is actual:
      return (!it->second.uses.empty() || it->second.payable != PayableStatus::Unknown);
    }
    return false;
  }

  // blocking call
  bool execute(/*[in,out]*/ SmartExecutionData& data);

  // blocking call
  bool update_metadata(const api::SmartContractInvocation& contract, StateItem& state);

  void add_uses_from(const csdb::Address& abs_addr, const std::string& method, std::vector<csdb::Address>& uses);

  // extracts and returns name of method executed by referenced transaction
  std::string print_executed_method(const SmartContractRef& ref);

  std::string get_executed_method_name(const SmartContractRef& ref);

  // calculates from block a one smart round costs
  csdb::Amount smart_round_fee(const csdb::Pool& block);

  // tests max fee amount and round-based timeout on executed smart contracts;
  // invoked after every new block appears in blockchain
  void test_exe_conditions(const csdb::Pool& block);

  bool in_known_contracts(const csdb::Address& addr) const
  {
    return (known_contracts.find(absolute_address(addr)) != known_contracts.cend());
  }

  bool is_locked(const csdb::Address& abs_addr) const {
    const auto it = known_contracts.find(abs_addr);
    if (it != known_contracts.cend()) {
      return it->second.is_locked;
    }
    // only known contracts are allowed to execute!
    return true;
  }

  void update_lock_status(const csdb::Address& abs_addr, bool value);

  void update_lock_status(const QueueItem& item, bool value)
  {
    update_lock_status(item.abs_addr, value);
    if (!item.uses.empty()) {
      for (const auto& u : item.uses) {
        update_lock_status(absolute_address(u), value);
      }
    }
  }

  std::optional<api::SmartContractInvocation> get_smart_contract_impl(const csdb::Transaction& tr);

  void on_execution_completed_impl(const SmartExecutionData& data);

  // exe_queue item modifiers
  
  void update_status(QueueItem & item, cs::RoundNumber r, SmartContractStatus status);

  bool start_consensus(QueueItem& item, const cs::TransactionsPacket& pack)
  {
    item.pconsensus = std::make_unique<SmartConsensus>();
    return item.pconsensus->initSmartRound(pack, this->pnode, this);
  }

  void test_contracts_locks();

  // returns 0 if any error
  uint64_t next_inner_id(const csdb::Address& addr) const;

  // tests conditions to allow contract execution if disabled
  bool test_executor_availability();

};

}  // namespace cs
