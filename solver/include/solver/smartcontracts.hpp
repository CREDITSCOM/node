#pragma once

#include <apihandler.hpp>
#include <csdb/address.hpp>
#include <csdb/user_field.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <lib/system/signals.hpp>
#include <lib/system/common.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>

#include <optional>
#include <vector>

class BlockChain;

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
      constexpr csdb::user_field_id_t Calls = 0;
      // reference to last state transaction
      constexpr csdb::user_field_id_t RefState = 1;
      // count of user fields
      constexpr size_t Count = 2;
    }
    // new state transaction fields
    namespace new_state
    {
      // new state value, new byte-code (string)
      constexpr csdb::user_field_id_t Value = 0; // see apihandler.cpp #9 for currently used value ~1
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
    csdb::Pool::sequence_t sequence;
    // transaction sequence in block, instead of ID
    size_t transaction;

    // "serialization" methods

    csdb::UserField to_user_field() const
    {
      cs::Bytes data;
      cs::DataStream stream(data);
      stream << hash << sequence << transaction;
      return csdb::UserField(std::string(data.cbegin(), data.cend()));
    }

    void from_user_field(csdb::UserField fld)
    {
      std::string data = fld.value<std::string>();
      cs::DataStream stream(data.c_str(), data.size());
      stream >> hash >> sequence >> transaction;
    }
  };

  inline bool operator==(const SmartContractRef& l, const SmartContractRef& r)
  {
    return (l.transaction == r.transaction && l.hash == r.hash);
  }

  enum class SmartContractStatus
  {
    Finished = 0,
    Running,
    Waiting
  };

  using SmartContractExecutedSignal = cs::Signal<void(csdb::Transaction)>;

  class SmartContracts final
  {
  public:

    explicit SmartContracts(BlockChain&);

    SmartContracts() = delete;
    SmartContracts(const SmartContracts&) = delete;

    ~SmartContracts();

    void init(const cs::PublicKey&, Node::api_handler_ptr_t);

    // test transaction methods

    static bool is_smart_contract(const csdb::Transaction);
    static bool is_deploy(const csdb::Transaction);
    static bool is_start(const csdb::Transaction);
    static bool is_new_state(const csdb::Transaction);

    std::optional<api::SmartContractInvocation> get_smart_contract(const csdb::Transaction tr);

    static csdb::Transaction get_transaction(BlockChain& storage, const SmartContractRef& contract);
    
    // non-static variant
    csdb::Transaction get_transaction(const SmartContractRef& contract) const
    {
      return SmartContracts::get_transaction(bc, contract);
    }

    SmartContractStatus enqueue(csdb::Pool block, size_t trx_idx);
    void on_completed(csdb::Pool block, size_t trx_idx);

    void set_execution_result(csdb::Transaction tr)
    {
      emit signal_smart_executed(tr);
    }

    const char* name() const
    {
      return "Smarts";
    }

    void disable_execution()
    {
      execution_allowed = false;
    }

    void enable_execution()
    {
      execution_allowed = true;
    }

    void always_execute(bool val)
    {
      force_execution = val;
    }

  public signals:

    SmartContractExecutedSignal signal_smart_executed;

  private:

    using trx_innerid_t = int64_t; // see csdb/transaction.hpp near #101

    BlockChain& bc;
    cs::Bytes node_id;

    bool execution_allowed;
    bool force_execution;

    struct QueueItem
    {
      SmartContractRef contract;
      SmartContractStatus status;
      cs::RoundNumber round;
    };

    std::vector<QueueItem> exe_queue;

    std::vector<QueueItem>::const_iterator find_in_queue(const SmartContractRef& item)
    {
      auto it = exe_queue.cbegin();
      for(; it != exe_queue.cend(); ++it) {
        if(it->contract == item) {
          break;
        }
      }
      return it;
    }

    bool contains_me(const std::vector<cs::Bytes>& list)
    {
      return (list.cend() != std::find(list.cbegin(), list.cend(), node_id));
    }

    bool invoke_execution(const SmartContractRef& contract, csdb::Pool block);
  };

} // cs
