#pragma once

#include <apihandler.hpp>
#include <csdb/address.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <csdb/user_field.hpp>
#include <lib/system/common.hpp>
#include <lib/system/concurrent.hpp>
#include <lib/system/signals.hpp>
#include <lib/system/logger.hpp>

#include <csnode/node.hpp>  // introduce Node::api_handler_ptr_t

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
// transactions user fields
  namespace trx_uf
  {
    // deploy transaction fields
    namespace deploy
    {
      // byte-code (string)
      constexpr csdb::user_field_id_t Code = 0;
      // count of user fields
      constexpr size_t Count = 1;
    }  // namespace deploy
    // start transaction fields
    namespace start
    {
      // methods with args (string)
      constexpr csdb::user_field_id_t Methods = 0;
      // reference to last state transaction
      constexpr csdb::user_field_id_t RefState = 1;
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
  csdb::PoolHash hash;  // TODO: stop to use after loadBlock(sequence) works correctly
  // block sequence
  cs::Sequence sequence;
  // transaction sequence in block, instead of ID
  size_t transaction;

  SmartContractRef()
    : sequence(0)
    , transaction(0)
  {}

  SmartContractRef(const csdb::PoolHash block_hash, cs::Sequence block_sequence, size_t transaction_index)
    : hash(block_hash)
    , sequence(block_sequence)
    , transaction(transaction_index)
  {}

  SmartContractRef(const csdb::UserField user_field)
  {
    from_user_field(user_field);
  }

  bool is_valid() const
  {
    if(hash.is_empty()) {
      return false;
    }
    return (sequence != 0 || transaction != 0);
  }

  // "serialization" methods

  csdb::UserField to_user_field() const;
  void from_user_field(const csdb::UserField fld);

  csdb::TransactionID getTransactionID() const { return csdb::TransactionID(hash, transaction); }
};

struct SmartExecutionData {
  SmartContractRef contract_ref;
  std::string state;
  ::general::Variant ret_val;
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
  // is executing at the moment, is able to emit transactions
  Running,
  // is waiting until execution starts
  Waiting,
  // execution is finished, waiting for new state transaction, no more transaction emitting is allowed
  Finished,
  // contract is closed, neither new_state nor emitting transactions are allowed, should be removed from queue
  Closed
};

using SmartContractExecutedSignal = cs::Signal<void(cs::TransactionsPacket)>;

class SmartContracts final {
public:
  explicit SmartContracts(BlockChain&, CallsQueueScheduler&);

  SmartContracts() = delete;
  SmartContracts(const SmartContracts&) = delete;

  ~SmartContracts();

  void init(const cs::PublicKey&, Node* node);

  // test transaction methods

  // smart contract related transaction of any type
  static bool is_smart_contract(const csdb::Transaction);
  // deploy or start contract
  static bool is_executable(const csdb::Transaction tr);
  // deploy contract
  static bool is_deploy(const csdb::Transaction);
  // start contract
  static bool is_start(const csdb::Transaction);
  // new state of contract, result of invocation of executable transaction
  static bool is_new_state(const csdb::Transaction);

  /* Assuming deployer.is_public_key(), not a WalletId */
  static csdb::Address get_valid_smart_address(const csdb::Address& deployer, const uint64_t trId,
                                               const api::SmartContractDeploy&);

  bool is_payable_target( const csdb::Transaction tr );

  std::optional<api::SmartContractInvocation> get_smart_contract(const csdb::Transaction tr);

  static csdb::Transaction get_transaction(BlockChain& storage, const SmartContractRef& contract);

  // non-static variant
  csdb::Transaction get_transaction(const SmartContractRef& contract) const {
    return SmartContracts::get_transaction(bc, contract);
  }

  void enqueue(csdb::Pool block, size_t trx_idx);
  void on_new_state(csdb::Pool block, size_t trx_idx);

  bool set_execution_result(cs::TransactionsPacket& pack) const;

  // get & handle rejected transactions
  // usually ordinary consensus may reject smart-related transactions
  void on_reject(cs::TransactionsPacket& pack);

  csconnector::connector::ApiHandlerPtr get_api() const {
    return papi;
  }

  const char* name() const {
    return "Smart";
  }

  csdb::Address absolute_address(csdb::Address optimized_address) const {
    return bc.get_addr_by_type(optimized_address, BlockChain::ADDR_TYPE::PUBLIC_KEY);
  }

  bool is_running_smart_contract(csdb::Address addr) const;

  bool is_closed_smart_contract(csdb::Address addr) const;

  bool is_known_smart_contract(csdb::Address addr) const {
    return (known_contracts.find(absolute_address(addr)) != known_contracts.cend());
  }

  // return true if SmartContracts provide special handling for transaction
  bool capture(csdb::Transaction tr);

  bool execution_allowed;
  bool force_execution;

public signals:
  SmartContractExecutedSignal signal_smart_executed;

public slots:
  void on_execute_async_completed(const SmartExecutionData& data);

  // called when next block is stored
  void on_store_block(csdb::Pool block);

  // called when next block is read from database
  void on_read_block(csdb::Pool block, bool* should_stop);

private:
  using trx_innerid_t = int64_t;  // see csdb/transaction.hpp near #101

  const char *PayableName = "payable";
  const char *PayableRetType = "void";
  const char *PayableArgType = "java.lang.String";
  const char *PayableNameArg0 = "amount";
  const char *PayableNameArg1 = "currency";

  BlockChain& bc;
  CallsQueueScheduler& scheduler;
  cs::PublicKey node_id;
  // be careful, may be equal to nullptr if api is not initialized (for instance, blockchain failed to load)
  csconnector::connector::ApiHandlerPtr papi;

  CallsQueueScheduler::CallTag tag_cancel_running_contract;

  enum class PayableStatus : int {
    Unknown = -1,
    Absent = 0,
    Implemented = 1
  };

  struct StateItem {
    // reference to deploy transaction
    SmartContractRef deploy;
    // payable() method is implemented
    PayableStatus payable { PayableStatus::Unknown };
    // reference to last successful execution which state is stored by item, may be equal to deploy
    SmartContractRef execution;
    // current state which is result of last successful execution / deploy
    std::string state;
  };

  // last contract's state storage
  std::map<csdb::Address, StateItem> known_contracts;

  // async watchers
  std::list<cs::FutureWatcherPtr<SmartExecutionData>> executions_;

  struct QueueItem {
    // reference to smart in blockchain (block/transaction) that spawns execution
    SmartContractRef contract;
    // current status (running/waiting)
    SmartContractStatus status;
    // enqueue round
    cs::RoundNumber round_enqueue;
    // start round
    cs::RoundNumber round_start;
    // finish round
    cs::RoundNumber round_finish;
    // smart contract wallet/pub.key absolute address
    csdb::Address abs_addr;
    // 
    std::unique_ptr<SmartConsensus> pconsensus;

    QueueItem(const SmartContractRef& ref_contract, csdb::Address absolute_address)
      : contract(ref_contract)
      , status(SmartContractStatus::Waiting)
      , round_enqueue(0)
      , round_start(0)
      , round_finish(0)
      , abs_addr(absolute_address)
      , pconsensus(nullptr)
    {}

    void wait(cs::RoundNumber r)
    {
      round_enqueue = r;
      status = SmartContractStatus::Waiting;
      csdebug() << "Smart: contract is waiting from #" << r;
    }

    void start(cs::RoundNumber r)
    {
      round_start = r;
      status = SmartContractStatus::Running;
      csdebug() << "Smart: contract is running from #" << r;
    }

    void finish(cs::RoundNumber r)
    {
      round_finish = r;
      status = SmartContractStatus::Finished;
      csdebug() << "Smart: contract is finished on #" << r;
    }

    void close()
    {
      status = SmartContractStatus::Closed;
      csdebug() << "Smart: contract is closed";
    }
  };

  // executiom queue
  std::vector<QueueItem> exe_queue;
  Node* pnode;

  // locks 'emitted_transactions' when transaction is emitted by smart contract, or when it's time to collect them
  std::mutex mtx_emit_transaction;

  // emitted transactions if any while execution running
  std::map<csdb::Address, std::vector<csdb::Transaction>> emitted_transactions;

  void clear_emitted_transactions(const csdb::Address abs_addr);

  std::vector<QueueItem>::iterator find_in_queue(const SmartContractRef& item)
  {
    auto it = exe_queue.begin();
    for (; it != exe_queue.end(); ++it) {
      if (it->contract == item) {
        break;
      }
    }
    return it;
  }

  std::vector<QueueItem>::iterator find_in_queue(csdb::Address abs_addr)
  {
    auto it = exe_queue.begin();
    for(; it != exe_queue.end(); ++it) {
      if(it->abs_addr == abs_addr) {
        break;
      }
    }
    return it;
  }

  std::vector<QueueItem>::const_iterator find_in_queue(csdb::Address abs_addr) const
  {
    auto it = exe_queue.begin();
    for(; it != exe_queue.end(); ++it) {
      if(it->abs_addr == abs_addr) {
        break;
      }
    }
    return it;
  }

  void remove_from_queue(std::vector<QueueItem>::const_iterator it);

  void remove_from_queue(const SmartContractRef& item) {
    remove_from_queue(find_in_queue(item));
  }

  void checkAllExecutions();

  void test_exe_queue();

  bool contains_me(const std::vector<cs::PublicKey>& list) const {
    return (list.cend() != std::find(list.cbegin(), list.cend(), node_id));
  }

  // returns false if execution canceled, so caller is responsible to call remove_from_queue(item) method
  bool invoke_execution(const SmartContractRef& contract);

  // perform async execution via API to remote executor
  // is called from invoke_execution() method only
  // returns false if execution is canceled
  bool execute_async(const cs::SmartContractRef& item);

  // makes a transaction to store new_state of smart contract invoked by src
  // caller is responsible to test src is a smart-contract-invoke transaction
  csdb::Transaction result_from_smart_ref(const SmartContractRef& contract) const;

  // update in contracts table appropriate item's state
  bool update_contract_state(csdb::Transaction t, bool force_absolute_address = true);

  // get deploy info from cached deploy transaction reference
  std::optional<api::SmartContractInvocation> find_deploy_info(const csdb::Address abs_addr) const;

  // test if abs_addr is address of smart contract with payable() implemented;
  // may make a BLOCKING call to java executor
  bool is_payable(const csdb::Address abs_addr);

  // blocking call
  bool execute(const std::string& invoker, const std::string& smart_address, const api::SmartContractInvocation& contract, /*[in,out]*/ SmartExecutionData& data, uint32_t timeout_ms);

  // blocking call
  bool execute_payable(const std::string& invoker, const std::string& smart_address, const api::SmartContractInvocation& contract, /*[in,out]*/ SmartExecutionData& data, uint32_t timeout_ms,
    double amount);

  // blocking call
  bool implements_payable(const api::SmartContractInvocation& contract);

};

}  // namespace cs
