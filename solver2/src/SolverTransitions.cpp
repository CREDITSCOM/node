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
        m_pState = std::make_shared<NoState>();

        std::pair<Event, StatePtr> defaultRT { Event::RoundTable, pRTH };
        std::pair<Event, StatePtr> defaultBB { Event::BigBang, pBB };

        m_transitions = {
        { pStart, {
            defaultRT, defaultBB
        } },
        { pNormal, {
            defaultRT, defaultBB
        } },
        { pSync, {
            defaultRT, defaultBB
        } },
        { pTrusted, {
            defaultRT, defaultBB, { Event::Vectors, pTrustedV }, { Event::Matrices, pTrustedM }
        } },
        { pTrustedV, {
            defaultRT, defaultBB, { Event::Matrices, pTrustedVM }
        } },
        { pTrustedM, {
            defaultRT, defaultBB, { Event::Vectors, pTrustedVM }
        } },
        { pTrustedVM, {
            defaultRT, defaultBB, { Event::SetWriter, pWrite }
        } },
        { pCollect, {
            { Event::RoundTable, pNormal }, defaultBB
        } },
        { pWrite, {
            { Event::RoundTable, pCollect }, { Event::Hashes, pCollect }, defaultBB
        } },
        { pRTH, {
            { Event::SetNormal, pNormal }, { Event::SetTrusted, pTrusted }, defaultBB
        } },
        { pBB, {
            { Event::SetNormal, pNormal }, { Event::SetTrusted, pTrusted }, defaultRT
        } },
        { m_pState, {
            { Event::Start, pStart }
        } }
        };
    }

} // slv2
