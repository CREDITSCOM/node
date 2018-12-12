#include <solvercore.hpp>
#include <solvercontext.hpp>
#include <smartcontracts.hpp>
#include <states/nostate.hpp>

#pragma warning(push)
#pragma warning(disable : 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)

#include <csnode/spammer.hpp>
#include <csnode/walletsstate.hpp>

#include <lib/system/logger.hpp>

#include <functional>
#include <limits>
#include <sstream>
#include <string>

namespace cs
{

  // initial values for SolverCore options

  // To track timeout for active state
  constexpr const bool TimeoutsEnabled = false;
  // To enable make a transition to the same state
  constexpr const bool RepeatStateEnabled = true;
  // Special mode: uses debug transition table
  constexpr const bool DebugModeOn = false;
  // Special mode: uses monitor mode transition table
  constexpr const bool MonitorModeOn =
#if defined(MONITOR_NODE)
    true;
#else
    false;
#endif // MONITOR_NODE

  // default (test intended) constructor
  SolverCore::SolverCore()
    // options
    : opt_timeouts_enabled(TimeoutsEnabled)
    , opt_repeat_state_enabled(RepeatStateEnabled)
    , opt_mode(Mode::Default)
    // inner data
    , pcontext(std::make_unique<SolverContext>(*this))
    , tag_state_expired(CallsQueueScheduler::no_tag)
    , req_stop(true)
    , cnt_trusted_desired(Consensus::MinTrustedNodes)
    // consensus data
    , cur_round(0)
    // previous solver version instance
    , pnode(nullptr)
    , pws(nullptr)
    , psmarts(nullptr)
  {
    if constexpr(MonitorModeOn) {
      cslog() << "SolverCore: opt_monitor_mode is on, so use special transition table";
      InitMonitorModeTransitions();
    }
    else if constexpr(DebugModeOn) {
      cslog() << "SolverCore: opt_debug_mode is on, so use special transition table";
      InitDebugModeTransitions();
    }
    else if constexpr(true) {
      cslog() << "SolverCore: use default transition table";
      InitTransitions();
    }
  }

  // actual constructor
  SolverCore::SolverCore(Node* pNode, csdb::Address GenesisAddress, csdb::Address StartAddress)
    : SolverCore()
  {
    addr_genesis = GenesisAddress;
    addr_start = StartAddress;
    pnode = pNode;
    auto& bc = pNode->getBlockChain();
    pws = std::make_unique<cs::WalletsState>(bc);
    psmarts = std::make_unique<cs::SmartContracts>(bc);

    // bind signals
    cs::Connector::connect(&psmarts->signal_smart_executed, this, &cs::SolverCore::getSmartResultTransaction);
  }

  SolverCore::~SolverCore()
  {
    scheduler.Stop();
    transitions.clear();
  }

  void SolverCore::ExecuteStart(Event start_event)
  {
    if(!is_finished()) {
      cswarning() << "SolverCore: cannot start again, already started";
      return;
    }
    req_stop = false;
    handleTransitions(start_event);
  }

  void SolverCore::finish()
  {
    if(pstate) {
      pstate->off(*pcontext);
    }
    scheduler.RemoveAll();
    tag_state_expired = CallsQueueScheduler::no_tag;
    pstate = std::make_shared<NoState>();
    req_stop = true;
  }

  void SolverCore::setState(const StatePtr& pState)
  {
    if(!opt_repeat_state_enabled) {
      if(pState == pstate) {
        return;
      }
    }
    if(tag_state_expired != CallsQueueScheduler::no_tag) {
      // no timeout, cancel waiting
      scheduler.Remove(tag_state_expired);
      tag_state_expired = CallsQueueScheduler::no_tag;
    }
    else {
      // state changed due timeout from within expired state
    }

    if(pstate) {
      pstate->off(*pcontext);
    }
    if(Consensus::Log) {
      cslog() << "SolverCore: switch " << (pstate ? pstate->name() : "null") << " -> "
        << (pState ? pState->name() : "null");
    }
    pstate = pState;
    if(!pstate) {
      return;
    }
    pstate->on(*pcontext);

    // timeout handling
    if(opt_timeouts_enabled) {
      tag_state_expired =
        scheduler.InsertOnce(Consensus::DefaultStateTimeout,
          [this]() {
        cslog() << "SolverCore: state " << pstate->name() << " is expired";
        // clear flag to know timeout expired
        tag_state_expired = CallsQueueScheduler::no_tag;
        // control state switch
        std::weak_ptr<INodeState> p1(pstate);
        pstate->expired(*pcontext);
        if(pstate == p1.lock()) {
          // expired state did not change to another one, do it now
          cslog() << "SolverCore: there is no state set on expiration of " << pstate->name();
          // setNormalState();
        }
      },
          true /*replace if exists*/);
    }
  }

  void SolverCore::handleTransitions(Event evt)
  {
    if(!pstate) {
      // unable to work until initTransitions() called
      return;
    }
    if(Event::BigBang == evt) {
      cswarning() << "SolverCore: BigBang on";
    }
    const auto& variants = transitions[pstate];
    if(variants.empty()) {
      cserror() << "SolverCore: there are no transitions for " << pstate->name();
      return;
    }
    auto it = variants.find(evt);
    if(it == variants.cend()) {
      // such event is ignored in current state
      csdebug() << "SolverCore: event " << static_cast<int>(evt) << " ignored in state " << pstate->name();
      return;
    }
    setState(it->second);
  }

  bool SolverCore::stateCompleted(Result res)
  {
    if(Result::Failure == res) {
      cserror() << "SolverCore: error in state " << (pstate ? pstate->name() : "null");
    }
    return (Result::Finish == res);
  }

  void SolverCore::spawn_next_round(const std::vector<cs::PublicKey>& nodes, const std::vector<cs::TransactionsPacketHash>& hashes, std::string&& currentTimeStamp)
  {
    cslog() << "SolverCore: TRUSTED -> WRITER, do write & send block";

    cs::RoundTable table;
    table.round = cs::Conveyer::instance().currentRoundNumber() + 1;
    table.confidants = nodes;
    table.hashes = hashes;

    cslog() << "Applying next hashes to ROUND Table (" << hashes.size() << "):";
    for (std::size_t i = 0; i < hashes.size(); ++i) {
      csdebug() << i << ". " << hashes[i].toString();
    }

    pnode->prepareMetaForSending(table, currentTimeStamp);
  }

  void SolverCore::getSmartResultTransaction(const csdb::Transaction transaction) {
    StageOneSmarts stage;
    cscrypto::CalculateHash(stage.hash,transaction.to_byte_stream().data(), transaction.to_byte_stream().size());
    stage.sender = ownSmartsConfNum;
    pcontext->addSmartStage1(stage, true);  
  }


}  // namespace slv2
