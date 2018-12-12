#pragma once

#include <apihandler.hpp>
#include <csdb/address.hpp>
#include <csdb/user_field.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <lib/system/signals.hpp>

#include <optional>
#include <vector>
#include <sstream>

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
      // byte-code
      constexpr csdb::user_field_id_t Code = 0;
    }
    // start transaction fields
    namespace start
    {
      // methods with args
      constexpr csdb::user_field_id_t Calls = 0;
      // reference to last state transaction
      constexpr csdb::user_field_id_t RefState = 1;
    }
    // new state transaction fields
    namespace new_state
    {
      // new state value, new byte-code
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
      std::ostringstream os;
      os << sequence << '|' << transaction;
      return csdb::UserField(os.str());
    }

    void from_user_field(csdb::UserField)
    {
      std::istringstream is;
      char delim;
      is >> sequence >> delim >> transaction;
      //TODO: review this code
      assert(delim == '|');
    }
  };

  inline bool operator==(const SmartContractRef& l, const SmartContractRef& r)
  {
    return (l.transaction == r.transaction && l.hash == r.hash);
  }

  enum class SmartContractStatus
  {
    Running,
    Waiting
  };

  class SolverCore;

  using SmartContractExecutedSignal = cs::Signal<void(csdb::Transaction)>;

  class SmartContracts final
  {
  public:

    explicit SmartContracts(BlockChain&);

    SmartContracts() = delete;
    SmartContracts(const SmartContracts&) = delete;

    ~SmartContracts();

    // test transaction methods

    static bool is_smart_contract(const csdb::Transaction);
    static bool is_deploy(const csdb::Transaction);
    static bool is_start(const csdb::Transaction);
    static bool is_new_state(const csdb::Transaction);

    static std::optional<api::SmartContractInvocation> get_smart_contract(const csdb::Transaction tr);

    static csdb::Transaction get_transaction(BlockChain& storage, const SmartContractRef& contract);
    
    // non-static variant
    csdb::Transaction get_transaction(const SmartContractRef& contract) const
    {
      return SmartContracts::get_transaction(bc, contract);
    }

    std::pair<SmartContractStatus, const SmartContractRef&> enqueue(
      const csdb::PoolHash blk_hash, csdb::Pool::sequence_t blk_seq, size_t trx_idx, cs::RoundNumber round);

    void set_execution_result(csdb::Transaction tr)
    {
      emit signal_smart_executed(tr);
    }

  public signals:

    SmartContractExecutedSignal signal_smart_executed;

  private:

    using trx_innerid_t = int64_t; // see csdb/transaction.hpp near #101

    BlockChain& bc;

    struct QueueItem
    {
      SmartContractRef contract;
      SmartContractStatus status;
      cs::RoundNumber round;
    };

    std::vector<QueueItem> exe_queue;

  };

} // cs
