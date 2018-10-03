#include "SolverCore.h"
#include "SolverContext.h"
#include <Solver/Solver.hpp>
#include "Node.h"
#include "Generals.h"

#pragma warning(push)
#pragma warning(disable: 4324)
#include <sodium.h>
#pragma warning(pop)

#include <lib/system/logger.hpp>

#include <limits>
#include <string>

namespace slv2
{

    SolverCore::SolverCore()
        // options
        : opt_timeouts_enabled(false)
        , opt_repeat_state_enabled(true)
        , opt_spammer_on(false)
        , opt_is_proxy_v1(false)
        // inner data
        , pcontext(std::make_unique<SolverContext>(*this))
        //, scheduler()
        , tag_state_expired(CallsQueueScheduler::no_tag)
        , req_stop(true)
        //, transitions()
        //, pstate
        // consensus data
        , cur_round(0)
        //, public_key()
        //, private_key()
        , pown_hvec(std::make_unique<Credits::HashVector>())
        //, recv_vect()
        //, recv_matr()
        //, recv_hash()
        , last_trans_list_recv(std::numeric_limits<uint64_t>::max())
        //, pool()
        //, trans_mtx()
        //, transactions()
        // previous solver version instance
        , pslv_v1(nullptr)
        , pnode(nullptr)
        , pgen_inst(nullptr)
        , pgen(nullptr)
    {
        InitTransitions();
    }

    SolverCore::SolverCore(Node * pNode) : SolverCore()
    {
        if(opt_is_proxy_v1) {
            pslv_v1 = std::make_unique<Credits::Solver>(pNode);
            pgen = pslv_v1->generals.get();
        }
        else {
            pgen_inst = std::make_unique<Credits::Generals>();
            // temp decision until solver-1 may be instantiated:
            pgen = pgen_inst.get();
        }
        pnode = pNode;
    }

    SolverCore::~SolverCore()
    {
        scheduler.Stop();
        transitions.clear();
    }

    void SolverCore::start()
    {
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: starting in " << (opt_is_proxy_v1 ? "proxy" : "standalone") << " mode");
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
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: switch state " << pstate->name() << " -> " << pState->name());
        }
        pstate = pState;
        pstate->on(*pcontext);
        
        // timeout handling
        if(opt_timeouts_enabled) {
            tag_state_expired = scheduler.InsertOnce(Consensus::DefaultStateTimeout, [this]() {
                if(Consensus::Log) {
                    LOG_NOTICE("SolverCore: state " << pstate->name() << " is expired");
                }
                // clear flag to know timeout expired
                tag_state_expired = CallsQueueScheduler::no_tag;
                // control state switch
                std::weak_ptr<INodeState> p1(pstate);
                pstate->expired(*pcontext);
                if(pstate == p1.lock()) {
                    // expired state did not change to another one, do it now
                    if(Consensus::Log) {
                        LOG_NOTICE("SolverCore: there is no state set on expiration of " << pstate->name());
                    }
                    //setNormalState();
                }
            }, true);
        }
    }

    void SolverCore::handleTransitions(Event evt)
    {
        if(Event::BigBang == evt) {
            if(Consensus::Log) {
                LOG_WARN("SolverCore: BigBang on");
            }
        }
        const auto& variants = transitions [pstate];
        if(variants.empty()) {
            if(Consensus::Log) {
                LOG_ERROR("SolverCore: there are no transitions for " << pstate->name());
            }
            return;
        }
        auto it = variants.find(evt);
        if(it == variants.cend()) {
            // such event is ignored in current state
            if(Consensus::Log) {
                LOG_DEBUG("SolverCore: event " << static_cast<int>(evt) << "ignored in state " << pstate->name());
            }
            return;
        }
        setState(it->second);
    }

    bool SolverCore::stateCompleted(Result res)
    {
        if(Consensus::Log && Result::Failure == res) {
            LOG_ERROR("SolverCore: error in state " << pstate->name());
        }
        return (Result::Finish == res);
    }

    void SolverCore::repeatLastBlock()
    {
        if(cur_round == pool.sequence()) {
            // still actual, send it again
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: current block is ready to repeat");
            }
            sendBlock(pool);
        }
        else {
            // load block and send it
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: current block is out of date, so load stored block to repeat");
            }
            auto& bch = pnode->getBlockChain();
            csdb::Pool p = bch.loadBlock(bch.getLastWrittenHash());
            if(p.is_valid()) {
                sendBlock(p);
            }
        }
    }

    // Copied methods from solver.v1
    
    void SolverCore::sendBlock(csdb::Pool& p)
    {
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: sending block #" << p.sequence() << " of " << p.transactions_count() << " transactions");
        }
        pnode->sendBlock(p);
    }

    void SolverCore::storeBlock(csdb::Pool& p)
    {
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: storing block #" << p.sequence() << " of " << p.transactions_count() << " transactions");
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
