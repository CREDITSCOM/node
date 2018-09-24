#include "SolverCore.h"
#include "StartState.h"
#include "NormalState.h"
#include "NormalSyncState.h"
#include "TrustedState.h"
#include "TrustedCollectState.h"
#include "TrustedWriteState.h"

#include <iostream>

SolverCore::SolverCore()
    : m_stateExpiredTag(CallsQueueScheduler::no_tag)
    , m_shouldStop(false)
    , m_pState(&m_noState)
{
    m_pStartState = new StartState();
    m_pNormalState = new NormalState();
    m_pNormalSyncState = new NormalSyncState();
    m_pTrustedState = new TrustedState();
    m_pTrustedCollectState = new TrustedCollectState();
    m_pTrustedWriteState = new TrustedWriteState();

    setState(m_pStartState);
}

SolverCore::~SolverCore()
{
    m_scheduler.Stop();
    delete m_pStartState;
    delete m_pNormalState;
    delete m_pNormalSyncState;
    delete m_pTrustedState;
    delete m_pTrustedCollectState;
    delete m_pTrustedWriteState;
}

void SolverCore::setState(INodeState* pState)
{
    // To prevent state from extend itself uncomment next block
    //if(pState == m_pState) {
    //    return;
    //}
    if(m_stateExpiredTag != CallsQueueScheduler::no_tag) {
        // no timeout, cancel waiting
        m_scheduler.Remove(m_stateExpiredTag);
        m_stateExpiredTag = CallsQueueScheduler::no_tag;
    }
    else {
        // state changed due timeout from within expired state        
    }
    std::cout << "Change state: " << m_pState->getName() << " -> " << pState->getName() << std::endl;
    const INodeState* tmp = m_pState;
    m_pState->stateOff(*this);
    m_pState = pState;
    m_pState->stateOn(*this, *tmp);
    m_stateExpiredTag = m_scheduler.InsertOnce(DefaultStateTimeout, [this]() {
        std::cout << "State expired: " << m_pState->getName() << std::endl;
        // clear flag to know timeout expired
        m_stateExpiredTag = CallsQueueScheduler::no_tag;
        // control state switch
        INodeState * expired = m_pState;
        m_pState->stateExpired(*this);
        if(m_pState == expired) {
            // expired state did not change to another one, do it now
            std::cout << "expired state did not select new one, set NormalState (default)" << std::endl;
            setNormalState();
        }
    }, true);
}

void SolverCore::setStartState()
{
    setState(m_pStartState);
}

void SolverCore::setNormalState()
{
    setState(m_pNormalState);
}

void SolverCore::setNormalSyncState()
{
    setState(m_pNormalSyncState);
}

void SolverCore::setTrustedState()
{
    setState(m_pTrustedState);
}

void SolverCore::setTrustedCollectState()
{
    setState(m_pTrustedCollectState);
}

void SolverCore::setTrustedWriteState()
{
    setState(m_pTrustedWriteState);
}
