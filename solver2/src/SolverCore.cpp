#include "SolverCore.h"
#include <Solver/Solver.hpp>
#include <iostream>

namespace slv2
{

    SolverCore::SolverCore()
        // internal data
        : m_context(*this)
        , m_stateExpiredTag(CallsQueueScheduler::no_tag)
        , m_shouldStop(false)
        // options
        , m_optTimeoutsEnabled(false)
        , m_optDuplicateStateEnabled(true)
        // consensus data
        , m_round(0)
        , m_pNode(nullptr)
        , m_pGen(nullptr)
    {
        InitTransitions();
    }

    SolverCore::SolverCore(Node * pNode) : SolverCore()
    {
        m_pSolvV1 = (std::make_unique<Credits::Solver>(pNode));
        m_pGen = m_pSolvV1->generals.get();
        m_pNode = m_pSolvV1->node_;
    }

    SolverCore::~SolverCore()
    {
        m_scheduler.Stop();
        m_transitions.clear();
    }

    void SolverCore::Start()
    {
        m_shouldStop = false;
        handleTransitions(Event::Start);
    }

    void SolverCore::Finish()
    {
        m_pState->stateOff(m_context);
        m_scheduler.RemoveAll();
        m_stateExpiredTag = CallsQueueScheduler::no_tag;
        m_pState = nullptr;
        m_shouldStop = true;
    }

    void SolverCore::setState(const StatePtr& pState)
    {
        if(!m_optDuplicateStateEnabled) {
            if(pState == m_pState) {
                return;
            }
        }
        if(m_stateExpiredTag != CallsQueueScheduler::no_tag) {
            // no timeout, cancel waiting
            m_scheduler.Remove(m_stateExpiredTag);
            m_stateExpiredTag = CallsQueueScheduler::no_tag;
        }
        else {
            // state changed due timeout from within expired state        
        }
        
        m_pState->stateOff(m_context);
        std::cout << "Core: switch state " << m_pState->getName() << " -> " << pState->getName() << std::endl;
        m_pState = pState;
        m_pState->stateOn(m_context);
        
        // timeout hadling
        if(m_optTimeoutsEnabled) {
            m_stateExpiredTag = m_scheduler.InsertOnce(DefaultStateTimeout, [this]() {
                std::cout << "Core: state " << m_pState->getName() << " is expired" << std::endl;
                // clear flag to know timeout expired
                m_stateExpiredTag = CallsQueueScheduler::no_tag;
                // control state switch
                std::weak_ptr<INodeState> p1(m_pState);
                m_pState->stateExpired(m_context);
                if(m_pState == p1.lock()) {
                    // expired state did not change to another one, do it now
                    std::cout << "Core: there is no state set on expiration of " << m_pState->getName() << std::endl;
                    //setNormalState();
                }
            }, true);
        }
    }

    void SolverCore::handleTransitions(Event evt)
    {
        if(Event::BigBang == evt) {
            std::cout << "Core: BigBang on" << std::endl;
        }
        const auto& variants = m_transitions [m_pState];
        if(variants.empty()) {
            std::cout << "Core: there are no transitions for " << m_pState->getName() << std::endl;
            return;
        }
        auto it = variants.find(evt);
        if(it == variants.cend()) {
            // such event is ignored in current state
            return;
        }
        setState(it->second);
    }

    bool SolverCore::stateCompleted(Result res)
    {
        if(Result::Failure == res) {
            std::cout << "Core: handler error in state " << m_pState->getName() << std::endl;
        }
        return (Result::Finish == res);
    }

    // SolverCore::Context implementation
    
    void SolverContext::becomeNormal()
    {
        m_core.handleTransitions(SolverCore::Event::SetNormal);
    }

    void SolverContext::becomeTrusted()
    {
        m_core.handleTransitions(SolverCore::Event::SetTrusted);
    }

    void SolverContext::becomeWriter()
    {
        m_core.handleTransitions(SolverCore::Event::SetWriter);
    }

    void SolverContext::startNewRound()
    {
        m_core.beforeNextRound();
        m_core.nextRound();
    }


} // slv2
