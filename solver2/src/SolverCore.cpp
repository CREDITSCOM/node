#include "SolverCore.h"
#include <Solver/Solver.hpp>

#if ! defined(SOLVER_USES_PROXY_TYPES)
#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)
#endif

#include <iostream>

namespace slv2
{

    SolverCore::SolverCore()
        // internal data
        : context(*this)
        , tag_state_expired(CallsQueueScheduler::no_tag)
        , req_stop(true)
        // options
        , opt_timeouts_enabled(false)
        , opt_repeat_state_enabled(true)
        // consensus data
        , cur_round(0)
        , pnode(nullptr)
        , pgen(nullptr)
    {
        InitTransitions();
    }

    SolverCore::SolverCore(Node * pNode) : SolverCore()
    {
        pslv_v1 = (std::make_unique<Credits::Solver>(pNode));
        pgen = pslv_v1->generals.get();
        pnode = pslv_v1->node_;
    }

    SolverCore::~SolverCore()
    {
        scheduler.Stop();
        transitions.clear();
    }

    void SolverCore::start()
    {
        req_stop = false;
        handleTransitions(Event::Start);
    }

    void SolverCore::finish()
    {
        pstate->off(context);
        scheduler.RemoveAll();
        tag_state_expired = CallsQueueScheduler::no_tag;
        pstate = nullptr;
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
        
        pstate->off(context);
        std::cout << "Core: switch state " << pstate->name() << " -> " << pState->name() << std::endl;
        pstate = pState;
        pstate->on(context);
        
        // timeout hadling
        if(opt_timeouts_enabled) {
            tag_state_expired = scheduler.InsertOnce(Consensus::DefaultStateTimeout, [this]() {
                std::cout << "Core: state " << pstate->name() << " is expired" << std::endl;
                // clear flag to know timeout expired
                tag_state_expired = CallsQueueScheduler::no_tag;
                // control state switch
                std::weak_ptr<INodeState> p1(pstate);
                pstate->expired(context);
                if(pstate == p1.lock()) {
                    // expired state did not change to another one, do it now
                    std::cout << "Core: there is no state set on expiration of " << pstate->name() << std::endl;
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
        const auto& variants = transitions [pstate];
        if(variants.empty()) {
            std::cout << "Core: there are no transitions for " << pstate->name() << std::endl;
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
            std::cout << "Core: handler error in state " << pstate->name() << std::endl;
        }
        return (Result::Finish == res);
    }

    // Copied methods from solver.v1
    
    void SolverCore::prepareBlockAndSend()
    {
        addTimestampToPool(m_pool);
        m_pool.set_writer_public_key(public_key);
        m_pool.set_sequence((pnode->getBlockChain().getLastWrittenSequence()) + 1);
        m_pool.set_previous_hash(csdb::PoolHash::from_string(""));
        m_pool.sign(private_key);
        pnode->sendBlock(std::move(m_pool));
        pnode->getBlockChain().setGlobalSequence(static_cast<uint32_t>(m_pool.sequence()));
        pnode->getBlockChain().putBlock(m_pool);
    }

    void SolverCore::prepareBadBlockAndSend()
    {
        b_pool.set_sequence((pnode->getBlockChain().getLastWrittenSequence()) + 1);
        b_pool.set_previous_hash(csdb::PoolHash::from_string(""));
        pnode->sendBadBlock(std::move(b_pool));
    }

    void SolverCore::addTimestampToPool(csdb::Pool& pool)
    {
        pool.add_user_field(0, std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
        ));
    }

    // SolverCore::Context implementation
    
    void SolverContext::becomeNormal()
    {
        core.handleTransitions(SolverCore::Event::SetNormal);
    }

    void SolverContext::becomeTrusted()
    {
        core.handleTransitions(SolverCore::Event::SetTrusted);
    }

    void SolverContext::becomeWriter()
    {
        core.handleTransitions(SolverCore::Event::SetWriter);
    }

    void SolverContext::startNewRound()
    {
        core.beforeNextRound();
        core.nextRound();
    }

    void SolverContext::makeAndSendBlock()
    {
        core.prepareBlockAndSend();
    }

    void SolverContext::makeAndSendBadBlock()
    {
        core.prepareBadBlockAndSend();
    }

} // slv2
