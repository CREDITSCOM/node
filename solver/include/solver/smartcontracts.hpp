#pragma once

#include <apihandler.hpp>
#include <csdb/address.hpp>

#include <optional>
#include <vector>

class BlockChain;

namespace csdb
{
  class Transaction;
}

namespace cs
{

  class SmartContracts final
  {
  public:

    explicit SmartContracts(BlockChain&);

    SmartContracts() = delete;
    SmartContracts(const SmartContracts&) = delete;

    // test transaction methods
     
    static bool contains_smart_contract(const csdb::Transaction&);
    static std::optional<api::SmartContractInvocation> get_smart_contract(const csdb::Transaction& tr);

    // add / remove / test transactions as candidates to execute

    void add_exe_candidate(const csdb::Transaction& tr, cs::RoundNumber round)
    {
      if(find_exe_candidate(tr) == exe_candidates.cend()) {
        exe_candidates.emplace_back(TransactionMeta { tr.source(), tr.innerID(), round });
      }
    }

    bool has_exe_candidate(const csdb::Transaction& tr) const
    {
      return(find_exe_candidate(tr) != exe_candidates.cend());
    }

    void remove_exe_candidate(const csdb::Transaction& tr)
    {
      auto it = find_exe_candidate(tr);
      if(it != exe_candidates.cend()) {
        exe_candidates.erase(it);
      }
    }

    size_t cnt_exe_candidates() const
    {
      return exe_candidates.size();
    }

    void execute_candidates();

  private:
  
    using trx_innerid_t = int64_t; // see csdb/transaction.hpp near #101

    BlockChain& bc;


    struct TransactionMeta
    {
      csdb::Address source;
      trx_innerid_t inner_id;
      // round of the first "appearance"
      cs::RoundNumber round;
    };

    // storage of candidates for smart deployment/execution to execute upon conveyer sync completed
    std::vector<TransactionMeta> exe_candidates;

    // lookup in exe candidates
    std::vector<TransactionMeta>::const_iterator find_exe_candidate(const csdb::Transaction& tr) const;

  };

} // cs
