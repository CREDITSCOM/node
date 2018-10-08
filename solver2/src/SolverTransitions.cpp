#include "SolverCore.h"
#include "states/StartState.h"
#include "states/NormalState.h"
#include "states/SyncState.h"
#include "states/TrustedState.h"
#include "states/TrustedVState.h"
#include "states/TrustedMState.h"
#include "states/TrustedVMState.h"
#include "states/CollectState.h"
#include "states/WriteState.h"
#include "states/HandleRTState.h"
#include "states/HandleBBState.h"
#include "states//StartNormalState.h"
#include "states/StartCollectState.h"
#include "states/StartTrustedState.h"
#include "states/NoState.h"

namespace slv2
{

    void SolverCore::InitTransitions()
    {
        StatePtr pStart = std::make_shared<StartState>();
        StatePtr pNormal = std::make_shared<NormalState>();
        StatePtr pSync = std::make_shared<SyncState>();
        StatePtr pTrusted = std::make_shared<TrustedState>();
        StatePtr pTrustedV = std::make_shared<TrustedVState>();
        StatePtr pTrustedM = std::make_shared<TrustedMState>();
        StatePtr pTrustedVM = std::make_shared<TrustedVMState>();
        StatePtr pCollect = std::make_shared<CollectState>();
        StatePtr pWrite = std::make_shared<WriteState>();
        StatePtr pRTH = std::make_shared<HandleRTState>();
        StatePtr pBB = std::make_shared<HandleBBState>();
        StatePtr pStartNormal = std::make_shared<StartNormalState>();
        StatePtr pStartTrusted = std::make_shared<StartTrustedState>();
        StatePtr pStartCollect = std::make_shared<StartCollectState>();
        StatePtr pNone = std::make_shared<NoState>();
        // start with that:
        pstate = pNone;

        std::pair<Event, StatePtr> defaultRT { Event::RoundTable, pRTH };
        std::pair<Event, StatePtr> defaultBB { Event::BigBang, pBB };

        transitions = {

            // transition NoState -> Start on the first round
        { pNone, {
            { Event::RoundTable, pStart }
        } },

            // pStart acts as round table handler on the first round
        { pStart, {
            { Event::SetNormal, pStartNormal }, { Event::SetTrusted, pStartTrusted }, { Event::SetCollector, pStartCollect }
        } },

            // Normal state analog for the first round
        { pStartNormal, {
            defaultRT
        } },

            // Trusted state analog for the first round
        { pStartTrusted, {
            defaultRT, { Event::Vectors, pTrustedV }, { Event::Matrices, pTrustedM }
        } },

            // Collect state analog for the first round
        { pStartCollect, {
            defaultRT
        } },

            // Normal state (normal node)
        { pNormal, {
            defaultRT
        } },

            // Sync state, not useful for now due to Node implements sync process
        { pSync, {
            defaultRT
        } },

            // Trusted state (confidant node)
        { pTrusted, {
            defaultRT, { Event::Vectors, pTrustedV }, { Event::Matrices, pTrustedM }
        } },

            // Trusted state after enough vectors are received but not enough matrices (confidant node)
        { pTrustedV, {
            defaultRT, { Event::Matrices, pTrustedVM }
        } },

            // Trusted state after enough matrices are received but not enough vectors (confidant node)
        { pTrustedM, {
            defaultRT, { Event::Vectors, pTrustedVM }
        } },

            // Trusted state after enough both vectors and matrices are received (confidant node)
        { pTrustedVM, {
            defaultRT, { Event::SetWriter, pWrite }
        } },

            // Collect state (main node)
        { pCollect, {
            defaultRT
        } },

            // Write state (writing node)
        { pWrite, {
            { Event::RoundTable, pCollect }/*defaultRT*/, { Event::Hashes, pCollect }, defaultBB
        } },

            // round table handler
        { pRTH, {
            { Event::SetNormal, pNormal }, { Event::SetTrusted, pTrusted }, { Event::SetCollector, pCollect }
        } },

            // BigBang handler, not useful for now due to Node implements BigBang handling
        { pBB, {
            // only option to activate BB is from Write, so we will act almost as Write and finishing round upon receive hashes
            defaultRT
        } }

        };
    }

} // slv2
