#pragma once

#include <apihandler.hpp>
#include <csdb/address.hpp>
#include <csdb/user_field.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <lib/system/signals.hpp>
#include <lib/system/common.hpp>
#include <lib/system/concurrent.hpp>

#include <csnode/node.hpp> // introduce Node::api_handler_ptr_t

#include <optional>
#include <vector>
#include <list>
#include <mutex>

//#define DEBUG_SMARTS

class BlockChain;
class CallsQueueScheduler;

namespace csdb
{
  class Transaction;
}

namespace cs
{
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
    }
    // start transaction fields
    namespace start
    {
      // methods with args (string)
      constexpr csdb::user_field_id_t Methods = 0;
      // reference to last state transaction
      constexpr csdb::user_field_id_t RefState = 1;
      // count of user fields, may vary from 1 (source is person) to 2 (source is another contract)
      //constexpr size_t Count = {1,2};
    }
    // new state transaction fields
    namespace new_state
    {
      // new state value, new byte-code (string)
      constexpr csdb::user_field_id_t Value = ~1; // see apihandler.cpp #9 for currently used value ~1
      // reference to start transaction
      constexpr csdb::user_field_id_t RefStart = 1;
      // fee value
      constexpr csdb::user_field_id_t Fee = 2;
      // count of user fields
      constexpr size_t Count = 3;
    }
    // smart-gen transaction field
    namespace smart_gen
    {
      // reference to start transaction
      constexpr csdb::user_field_id_t RefStart = 0;
    }
    // ordinary transaction field
    namespace ordinary
    {
      // no fields defined
    }
  }

  struct SmartContractRef
  {
    // block hash
    csdb::PoolHash hash; // TODO: stop to use after loadBlock(sequence) works correctly
    // block sequence
    cs::Sequence sequence;
    // transaction sequence in block, instead of ID
    size_t transaction;

    // "serialization" methods

    csdb::UserField to_user_field() const;

    void from_user_field(const csdb::UserField fld);
  };

  struct SmartExecutionData {
    csdb::Transaction transaction;
    std::string state;
    SmartContractRef smartContract;
    executor::ExecuteByteCodeResult result;
    std::string error;
  };

  inline bool operator==(const SmartContractRef& l, const SmartContractRef& r)
  {
    return (l.transaction == r.transaction && l.sequence == r.sequence /*&& l.hash == r.hash*/);
  }

  inline bool operator<(const SmartContractRef& l, const SmartContractRef& r)
  {
    if(l.sequence < r.sequence) {
      return true;
    }
    if(l.sequence > r.sequence) {
      return false;
    }
    return (l.transaction < r.transaction);
  }

  enum class SmartContractStatus
  {
    Running,
    Waiting,
    Finished
  };

  using SmartContractExecutedSignal = cs::Signal<void(cs::TransactionsPacket)>;

  class SmartContracts final
  {
  public:

    explicit SmartContracts(BlockChain&, CallsQueueScheduler&);

    SmartContracts() = delete;
    SmartContracts(const SmartContracts&) = delete;

    ~SmartContracts();

    void init(const cs::PublicKey&, csconnector::connector::ApiHandlerPtr);

    // test transaction methods

    // smart contract related transaction of any type
    static bool is_smart_contract(const csdb::Transaction);
    // deploy or start contract
    bool is_executable(const csdb::Transaction tr) const;
    // deploy contract
    bool is_deploy(const csdb::Transaction) const;
    // start contract
    bool is_start(const csdb::Transaction) const;
    // new state of contract, result of invocation of executable transaction
    bool is_new_state(const csdb::Transaction) const;

    /* Assuming deployer.is_public_key() */
    static csdb::Address get_valid_smart_address(const csdb::Address& deployer,
                                                 const uint64_t trId,
                                                 const api::SmartContractDeploy&);

    std::optional<api::SmartContractInvocation> get_smart_contract(const csdb::Transaction tr) const;

    static csdb::Transaction get_transaction(BlockChain& storage, const SmartContractRef& contract);

    // non-static variant
    csdb::Transaction get_transaction(const SmartContractRef& contract) const
    {
      return SmartContracts::get_transaction(bc, contract);
    }

    void enqueue(csdb::Pool block, size_t trx_idx);
    void on_completed(csdb::Pool block, size_t trx_idx);

    void set_execution_result(cs::TransactionsPacket& pack) const;

    // get & handle rejected transactions
    // usually ordinary consensus may reject smart-related transactions
    void on_reject(cs::TransactionsPacket& pack);

    csconnector::connector::ApiHandlerPtr get_api() const
    {
      return papi;
    }

    const char* name() const
    {
      return "Smart";
    }

    csdb::Address absolute_address(csdb::Address optimized_address) const
    {
      return bc.get_addr_by_type(optimized_address, BlockChain::ADDR_TYPE::PUBLIC_KEY);
    }

    bool is_running_smart_contract(csdb::Address addr) const;

    bool is_known_smart_contract(csdb::Address addr) const
    {
      return (contract_state.find(absolute_address(addr)) != contract_state.cend());
    }

    // return true if currently executed smart contract emits passed transaction
    bool test_smart_contract_emits(csdb::Transaction tr);

    bool execution_allowed;
    bool force_execution;

  public signals:
    SmartContractExecutedSignal signal_smart_executed;

  public slots:
    void onExecutionFinished(const SmartExecutionData& data);

    // called when next block is stored
    void onStoreBlock(csdb::Pool block);

    // called when next block is read from database
    void onReadBlock(csdb::Pool block, bool* should_stop);

  private:

    using trx_innerid_t = int64_t; // see csdb/transaction.hpp near #101

    BlockChain& bc;
    CallsQueueScheduler& scheduler;
    cs::PublicKey node_id;
    // be careful, may be equal to nullptr if api is not initialized (for instance, blockchain failed to load)
    csconnector::connector::ApiHandlerPtr papi;

    CallsQueueScheduler::CallTag tag_remove_finished_contract;
    CallsQueueScheduler::CallTag tag_cancel_running_contract;

    // last contract's state storage
    std::map<csdb::Address, std::string> contract_state;

    // async watchers
    std::list<cs::FutureWatcherPtr<SmartExecutionData>> executions_;

    struct QueueItem
    {
      // reference to smart in blockchain (block/transaction) that spawns execution
      SmartContractRef contract;
      // current status (running/waiting)
      SmartContractStatus status;
      // start round
      cs::RoundNumber round;
      // smart contract wallet/pub.key absolute address
      csdb::Address abs_addr;
      // emitted transactions if any while execution running
      std::vector<csdb::Transaction> created_transactions;
    };

    // executiom queue
    std::vector<QueueItem> exe_queue;

    // locks exe_queue when transaction emitted by smart contract
    std::mutex mtx_emit_transaction;

    std::vector<QueueItem>::iterator find_in_queue(const SmartContractRef& item)
    {
      auto it = exe_queue.begin();
      for(; it != exe_queue.end(); ++it) {
        if(it->contract == item) {
          break;
        }
      }
      return it;
    }

    void remove_from_queue(std::vector<QueueItem>::const_iterator it);

    void remove_from_queue(const SmartContractRef& item)
    {
      remove_from_queue(find_in_queue(item));
    }

    void checkAllExecutions();

    void cancel_running_smart_contract();

    void test_exe_queue();

    bool contains_me(const std::vector<cs::PublicKey>& list) const
    {
      return (list.cend() != std::find(list.cbegin(), list.cend(), node_id));
    }

    // returns false if execution canceled, so caller is responsible to call remove_from_queue(item) method
    bool invoke_execution(const SmartContractRef& contract);

    // perform async execution via api to remote executor
    // is called from invoke_execution() method only
    // returns false if execution is canceled
    bool execute(const cs::SmartContractRef& item);

    // makes a transaction to store new_state of smart contract invoked by src
    // caller is responsible to test src is a smart-contract-invoke transaction
    csdb::Transaction result_from_smart_ref(const SmartContractRef& contract) const;
  };

} // cs
