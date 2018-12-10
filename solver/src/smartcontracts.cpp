#include <smartcontracts.hpp>

#include <csdb/transaction.hpp>
#include <csdb/user_field.hpp>

#include <sstream>

namespace cs
{

  constexpr csdb::user_field_id_t SmartContractField = 0;
  constexpr csdb::user_field_id_t SmartStateField = ~1; // see apihandler.cpp #9

  /*explicit*/
  SmartContracts::SmartContracts(BlockChain& blockchain)
    : bc(blockchain)
  {

  }

  /*static*/
  bool SmartContracts::contains_smart_contract(const csdb::Transaction& tr)
  {
    return tr.user_field(SmartContractField).is_valid(); // see apihandler.cpp near #494
  }

  /*static*/
  std::optional<api::SmartContractInvocation> SmartContracts::get_smart_contract(const csdb::Transaction& tr)
  {
    const auto& smart_fld = tr.user_field(SmartContractField); // see apihandler.cpp near #494
    if(smart_fld.is_valid()) {
      return deserialize<api::SmartContractInvocation>(smart_fld.value<std::string>());
    }
    return std::nullopt;
  }

  // lookup in exe candidates
  std::vector<SmartContracts::TransactionMeta>::const_iterator SmartContracts::find_exe_candidate(const csdb::Transaction& tr) const
  {
    csdb::Address source = tr.source();
    trx_innerid_t id = tr.innerID();
    auto it = exe_candidates.cbegin();
    if(!exe_candidates.empty()) {
      for(; it != exe_candidates.cend(); ++it) {
        if(it->inner_id == id && it->source == source) {
          break;
        }
      }
    }
    return it;
  }

  void SmartContracts::execute_candidates()
  {
    // assume the method has called upon record related block to chain
    //csdb::Pool block = bc.loadBlock(bc.getLastWrittenSequence());
    csdb::Pool block = bc.loadBlock(bc.getLastWrittenHash());
    if(block.transactions_count() == 0) {
      cserror() << "Smarts: last written block does not contain trx with SC";
    }
    else {
      // outer search through trx to provide the same order of execution on every node
      size_t i = 0;
      for(const auto& tr : block.transactions()) {
        // inner search through candidates on particular node
        const auto src = tr.source();
        const auto id = tr.innerID();
        if(contains_smart_contract(tr)) {
          const auto maybe_sc = get_smart_contract(tr);
          if(!maybe_sc.has_value()) {
            cserror() << "Smarts: get SC failed, skip execution";
            ++i;
            continue;
          }
          const auto it = find_exe_candidate(tr);
          if(exe_candidates.cend() == it) {
            cswarning() << "Smarts: ready to execute SC from unknown trx, block #" << block.sequence() << ", trx #" << i;
          }
          else {
            cslog() << "Smarts: ready to execute SC, block #" << block.sequence() << ", trx #" << i << ", from round " << it->round;
            exe_candidates.erase(it);
          }
          const auto& sc = maybe_sc.value();
          std::ostringstream os;
          cslog() << "------------------- Smart contract -------------------";
          cslog() << sc.smartContractDeploy.sourceCode;
          cslog() << "------------------------------------------------------";
        }
        ++i;
      }
    }
    if(!exe_candidates.empty()) {
      csdebug() << "Smarts: " << exe_candidates.size() << " SC is/are wait until to be in block to execute";
    }
  }

} // cs
