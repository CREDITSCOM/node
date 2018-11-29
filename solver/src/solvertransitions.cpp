#include <solvercore.hpp>
#include <states/handlebbstate.hpp>
#include <states/handlertstate.hpp>
#include <states/nostate.hpp>
#include <states/normalstate.hpp>
#include <states/primitivewritestate.hpp>
#include <states/syncstate.hpp>
#include <states/trustedpoststagestate.hpp>
#include <states/trustedstage1state.hpp>
#include <states/trustedstage2state.hpp>
#include <states/trustedstage3state.hpp>
#include <states/waitingstate.hpp>
#include <states/writingstate.hpp>

namespace cs
{

  void SolverCore::InitTransitions()
  {
    opt_mode = Mode::Default;

    StatePtr pNormal = std::make_shared<NormalState>();
    StatePtr pSync = std::make_shared<SyncState>();
    StatePtr pTrusted1 = std::make_shared<TrustedStage1State>();
    StatePtr pTrusted2 = std::make_shared<TrustedStage2State>();
    StatePtr pTrusted3 = std::make_shared<TrustedStage3State>();
    StatePtr pTrustedPost = std::make_shared<TrustedPostStageState>();
    StatePtr pRTH = std::make_shared<HandleRTState>();
    StatePtr pBB = std::make_shared<HandleBBState>();
    StatePtr pNone = std::make_shared<NoState>();
    StatePtr pWriting = std::make_shared<WritingState>();
    StatePtr pWaiting = std::make_shared<WaitingState>();

    StatePtr pWrite = std::make_shared<PrimitiveWriteState>();
    // start with that:
    pstate = pNone;

    std::pair<Event, StatePtr> defaultRT { Event::RoundTable, pRTH };
    std::pair<Event, StatePtr> defaultBB { Event::BigBang, pBB };

    transitions = {

      // transition NoState -> Start on the first round
      {pNone, {{Event::Start, pNormal}, {Event::SetTrusted, pWrite}, defaultRT}},

      // Normal state (normal node)
      {pNormal, {defaultRT, defaultBB}},

      // Sync state, not useful for now due to Node implements sync process
      {pSync, {defaultRT, defaultBB}},

      // Trusted stage 1: preparing characteristic function and gaethering hashes form all nodes -> sending message of
      // Stage1 to all trusted
      {pTrusted1, {defaultRT, defaultBB, {Event::Transactions, pTrusted2}, {Event::Hashes, pTrusted2}}},

      // Trusted state 2 after enough Stage1 messages are received (confidant node)
      {pTrusted2, {defaultRT, defaultBB, {Event::Stage1Enough, pTrusted3}}},

      // Trusted state 3 after enough Stage2 messages are received (confidant node)
      {pTrusted3, {defaultRT, defaultBB, {Event::Stage2Enough, pTrustedPost}}},

      // Trusted PostStageState after enough Stage3 (confirmation)are received (confidant node)
      {pTrustedPost, {defaultRT, defaultBB, {Event::Stage3Enough, pWaiting}}},

      // Trusted pre-Writing (confidant node)
      {pWaiting, {defaultRT, defaultBB, {Event::SetWriter, pWriting}}},

      // round table handler
      {pRTH, {{Event::SetNormal, pNormal}, {Event::SetTrusted, pTrusted1}}},

      // BigBang handler, not useful for now due to Node implements BigBang handling
      {pBB, { defaultRT}},

      // post-writing transition upon RoundTable && BigBang
      {pWriting, {defaultRT, defaultBB}}
    };
  }

  void SolverCore::InitDebugModeTransitions()
  {
    opt_mode = Mode::Debug;

    StatePtr pNormal = std::make_shared<NormalState>();
    StatePtr pWrite = std::make_shared<PrimitiveWriteState>();
    StatePtr pNone = std::make_shared<NoState>();
    // start with that:
    pstate = pNone;

    transitions = {

      // transition on the first round
      {pNone, {{Event::SetTrusted, pWrite}, {Event::SetNormal, pNormal}}},

      // transition Normal -> Write every round
      {pNormal, {{Event::RoundTable, pWrite}}},

      // transition Write -> Normal every round
      {pWrite, {{Event::RoundTable, pNormal}}},

    };
  }

  void SolverCore::InitMonitorModeTransitions()
  {
    opt_mode = Mode::Monitor;

    StatePtr pNormal = std::make_shared<NormalState>();
    StatePtr pNone = std::make_shared<NoState>();
    // start with that:
    pstate = pNone;

    transitions = {
      // transition on the first round
      {pNone, {{Event::Start, pNormal}}}
    };
  }
} // namespace slv2
