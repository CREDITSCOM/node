#include "SolverCore.h"
#include "SolverContext.h"
#include <Solver/Solver.hpp>
#include "../Node.h"

#pragma warning(push)
#pragma warning(disable: 4324)
#include <sodium.h>
#pragma warning(pop)

#include <iostream>
#include <limits>

namespace slv2
{

    SolverCore::SolverCore()
        // internal data
        : tag_state_expired(CallsQueueScheduler::no_tag)
        , req_stop(true)
        , pcontext(std::make_unique<SolverContext>(*this))
        // options
        , opt_timeouts_enabled(false)
        , opt_repeat_state_enabled(true)
        , opt_is_proxy_v1(false)
        // consensus data
        , cur_round(0)
        , pnode(nullptr)
        , pgen(nullptr)
        , pslv_v1(nullptr)
        , pown_hvec(std::make_unique<Credits::HashVector>())
        , last_trans_list_recv(std::numeric_limits<uint64_t>::max())
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
        if(Consensus::Log) {
            std::cout << "SolverCore: starting in " << (opt_is_proxy_v1 ? "proxy" : "standalone") << " mode" << std::endl;
        }
        req_stop = false;
        handleTransitions(Event::Start);
    }

    void SolverCore::finish()
    {
        pstate->off(*pcontext);
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
        
        pstate->off(*pcontext);
        std::cout << "SolverCore: switch state " << pstate->name() << " -> " << pState->name() << std::endl;
        pstate = pState;
        pstate->on(*pcontext);
        
        // timeout hadling
        if(opt_timeouts_enabled) {
            tag_state_expired = scheduler.InsertOnce(Consensus::DefaultStateTimeout, [this]() {
                std::cout << "SolverCore: state " << pstate->name() << " is expired" << std::endl;
                // clear flag to know timeout expired
                tag_state_expired = CallsQueueScheduler::no_tag;
                // control state switch
                std::weak_ptr<INodeState> p1(pstate);
                pstate->expired(*pcontext);
                if(pstate == p1.lock()) {
                    // expired state did not change to another one, do it now
                    std::cout << "SolverCore: there is no state set on expiration of " << pstate->name() << std::endl;
                    //setNormalState();
                }
            }, true);
        }
    }

    void SolverCore::handleTransitions(Event evt)
    {
        if(Event::BigBang == evt) {
            std::cout << "SolverCore: BigBang on" << std::endl;
        }
        const auto& variants = transitions [pstate];
        if(variants.empty()) {
            std::cout << "SolverCore: there are no transitions for " << pstate->name() << std::endl;
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
        if(Consensus::Log && Result::Failure == res) {
            std::cout << "SolverCore: error in state " << pstate->name() << std::endl;
        }
        return (Result::Finish == res);
    }

    void SolverCore::repeatLastBlock()
    {
        //if(cur_round == pool.sequence()) {
        //    // still actual, send it again
        //    if(Consensus::Log) {
        //        std::cout << "SolverCore: current block is ready to repeat" << std::endl;
        //    }
        //    sendBlock(pool);
        //}
        //else {
            // load block and send it
            if(Consensus::Log) {
                std::cout << "SolverCore: current block is out of date, so load stored block to repeat" << std::endl;
            }
            auto& bch = pnode->getBlockChain();
            csdb::Pool p = bch.loadBlock(bch.getLastWrittenHash());
            if(p.is_valid()) {
                sendBlock(p);
            }
        //}
    }

    // Copied methods from solver.v1
    
    void SolverCore::sendBlock(csdb::Pool& p)
    {
        if(Consensus::Log) {
            std::cout << "SolverCore: sending block #" << p.sequence() << " of " << p.transactions_count() << " transactions" << std::endl;
        }
        pnode->sendBlock(p);
    }

    void SolverCore::storeBlock(csdb::Pool& p)
    {
        if(Consensus::Log) {
            std::cout << "SolverCore: storing block #" << p.sequence() << " of " << p.transactions_count() << " transactions" << std::endl;
        }
        pnode->getBlockChain().setGlobalSequence(static_cast<uint32_t>(p.sequence()));
        pnode->getBlockChain().putBlock(p);
    }

    void SolverCore::prepareBlock(csdb::Pool& p)
    {
        // add timestamp
        p.add_user_field(0, std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
        ));
        // finalize
        pool.set_writer_public_key(public_key);
        pool.set_sequence((pnode->getBlockChain().getLastWrittenSequence()) + 1);
        pool.set_previous_hash(csdb::PoolHash::from_string(""));
        pool.sign(private_key);
    }

    void SolverCore::flushTransactions()
    {
        // thread-safe with send_wallet_transaction(), suppose to sync with calls from network-related threads
        std::lock_guard<std::mutex> l(trans_mtx);
        if(!transactions.empty()) {
            pnode->sendTransaction(std::move(transactions));
            transactions.clear();
        }
    }

    bool SolverCore::verify_signature(const csdb::Transaction& tr)
    {
        std::vector<uint8_t> message = tr.to_byte_stream_for_sig();
        std::vector<uint8_t> pub_key = tr.source().public_key();
        std::string signature = tr.signature();
        // if crypto_sign_ed25519_verify_detached(...) returns 0 - succeeded, 1 - failed
        return (0 == crypto_sign_ed25519_verify_detached((uint8_t *) signature.data(), message.data(), message.size(), pub_key.data()) );
    }

} // slv2
