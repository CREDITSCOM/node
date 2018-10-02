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
        // start with that:
        pstate = std::make_shared<NoState>();

        std::pair<Event, StatePtr> defaultRT { Event::RoundTable, pRTH };
        std::pair<Event, StatePtr> defaultBB { Event::BigBang, pBB };

        transitions = {
        { pStart, {
            defaultRT
        } },
        { pNormal, {
            defaultRT
        } },
        { pSync, {
            defaultRT
        } },
        { pTrusted, {
            defaultRT, { Event::Vectors, pTrustedV }, { Event::Matrices, pTrustedM }
        } },
        { pTrustedV, {
            defaultRT, { Event::Matrices, pTrustedVM }
        } },
        { pTrustedM, {
            defaultRT, { Event::Vectors, pTrustedVM }
        } },
        { pTrustedVM, {
            defaultRT, { Event::SetWriter, pWrite }
        } },
        { pCollect, {
            // commented out transition is correct almost always but is not after BigBang
            defaultRT // { Event::RoundTable, pNormal }
        } },
        { pWrite, {
            { Event::RoundTable, pCollect }/*defaultRT*/, { Event::Hashes, pCollect }, defaultBB
        } },
        { pRTH, {
            { Event::SetNormal, pNormal }, { Event::SetTrusted, pTrusted }, { Event::SetCollector, pCollect }
        } },
        { pBB, {
            // only option to activate BB is from Write, so act almost as Write and finishing round upon receive hashes
            defaultRT
        } },
        { pstate, {
            { Event::Start, pStart }
        } }
        };
    }

} // slv2
