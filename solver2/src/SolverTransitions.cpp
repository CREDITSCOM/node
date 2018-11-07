#include <SolverCore.h>
#include <states/NormalState.h>
#include <states/SyncState.h>
#include <states/TrustedStage1State.h>
#include <states/TrustedStage2State.h>
#include <states/TrustedStage3State.h>
#include <states/TrustedPostStageState.h>
#include <states/HandleRTState.h>
#include <states/HandleBBState.h>
#include <states/PrimitiveWriteState.h>
#include <states/NoState.h>
#include <states/WritingState.h>
#include <states/WaitingState.h>

namespace slv2
{

    void SolverCore::InitTransitions()
    {
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
        { pNone, {
            { Event::Start, pNormal }, { Event::SetTrusted, pWrite }, defaultRT
        } },

            // Normal state (normal node)
        { pNormal, {
            defaultRT, defaultBB
        } },

            // Sync state, not useful for now due to Node implements sync process
        { pSync, {
            defaultRT, defaultBB
        } },

            // Trusted stage 1: preparing characteristic function and gaethering hashes form all nodes -> sending message of Stage1 to all trusted
        { pTrusted1, {
            defaultRT, defaultBB, { Event::Transactions, pTrusted2 }, { Event::Hashes, pTrusted2 }
        } },

            // Trusted state 2 after enough Stage1 messages are received (confidant node)
        { pTrusted2, {
            defaultRT, defaultBB, { Event::Stage1Enough, pTrusted3 }
        } },

            // Trusted state 3 after enough Stage2 messages are received (confidant node)
        { pTrusted3, {
            defaultRT, defaultBB, { Event::Stage2Enough, pTrustedPost }
        } },

          // Trusted PostStageState after enough Stage3 (confirmation)are received (confidant node)
        { pTrustedPost, {
            defaultRT, defaultBB, { Event::Stage3Enough, pWaiting }
        } },

            // Trusted pre-Writing (confidant node)
        { pWaiting, {
            defaultRT, defaultBB, { Event::SetWriter, pWriting }
        } },

            // round table handler
        { pRTH, {
            { Event::SetNormal, pNormal }, { Event::SetTrusted, pTrusted1 }
        } },

            // BigBang handler, not useful for now due to Node implements BigBang handling
        { pBB, {
            // only option to activate BB is from Write, so we will act almost as Write and finishing round upon receive hashes
            defaultRT
        } },

            // transition PermanentWrite -> PermanentWrite on the first round
        { pWriting, {
            defaultRT, defaultBB
        } }

        };
    }

    void SolverCore::InitDebugModeTransitions()
    {
        StatePtr pNormal = std::make_shared<NormalState>();
        StatePtr pWrite = std::make_shared<PrimitiveWriteState>();
        StatePtr pNone = std::make_shared<NoState>();
        // start with that:
        pstate = pNone;

        transitions = {

        { pNone, {
            { Event::SetTrusted, pWrite }, { Event::SetNormal, pNormal }
        } },

            // transition Normal -> Normal on the first round
        { pNormal, {
            { Event::RoundTable, pWrite }
        } },

            // transition PermanentWrite -> PermanentWrite on the first round
        { pWrite, {
            { Event::RoundTable, pNormal }
        } },

        };
    }
} // slv2
