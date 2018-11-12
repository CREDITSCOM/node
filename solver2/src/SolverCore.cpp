#include <SolverCore.h>
#include <SolverContext.h>
#include <CallsQueueScheduler.h>
#include <Consensus.h>
#include <Stage.h>
#include <states/NoState.h>

#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <Solver/Solver.hpp>
#pragma warning(pop)

#include <Solver/WalletsState.h>
#include <Solver/Fee.h>
#include <Solver/spammer.h>

#pragma warning(push)
#pragma warning(disable: 4324)
#include <sodium.h>
#pragma warning(pop)

#include <lib/system/logger.hpp>

#include <limits>
#include <string>
#include <sstream>
#include <functional>

namespace slv2
{

    // initial values for SolverCore options

    // To track timeout for active state
    constexpr const bool TimeoutsEnabled = false;
    // To enable make a transition to the same state
    constexpr const bool RepeatStateEnabled = true;
    // to activate transaction spammer in normal state; currently, define SPAMMER 'in params.hpp' overrides this value
    constexpr const bool SpammerOn = true;
    // To turn on proxy mode to old solver-1 (SolverCore becomes completely "invisible")
    constexpr const bool ProxyToOldSolver = false;
    // Special mode: uses debug transition table
    constexpr const bool DebugModeOn = false;

    // default (test intended) constructor
    SolverCore::SolverCore()
        // options
        : opt_timeouts_enabled(TimeoutsEnabled)
        , opt_repeat_state_enabled(RepeatStateEnabled)
        , opt_spammer_on(SpammerOn)
        , opt_is_proxy_v1(ProxyToOldSolver)
        , opt_debug_mode(DebugModeOn)
        // inner data
        , pcontext(std::make_unique<SolverContext>(*this))
        , tag_state_expired(CallsQueueScheduler::no_tag)
        , req_stop(true)
        , cnt_trusted_desired(Consensus::MinTrustedNodes)
        , total_recv_trans(0)
        , total_accepted_trans(0)
        , cnt_deferred_trans(0)
        , total_duration_ms(0)
        // consensus data
        , addr_spam(std::nullopt)
        , cur_round(0)
        , pfee(std::make_unique<cs::Fee>())
        , is_bigbang(false)
        // previous solver version instance
        , pslv_v1(nullptr)
        , pnode(nullptr)
        , pws_inst(nullptr)
        , pws(nullptr)
        , pspam(nullptr)
    {
        if(!opt_debug_mode) {
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: use default transition table");
            }
            InitTransitions();
        }
        else {
            if(Consensus::Log) {
                LOG_WARN("SolverCore: opt_debug_mode is on, so use special transition table");
            }
            InitDebugModeTransitions();
        }
        if(opt_is_proxy_v1) {
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: serve as proxy to Solver-1");
            }
            InitTransitions();
        }
    }

    // actual constructor
    SolverCore::SolverCore(Node * pNode, csdb::Address GenesisAddress, csdb::Address StartAddress, std::optional<csdb::Address> SpammerAddress /*= {}*/)
        : SolverCore()
    {
        addr_genesis = GenesisAddress;
        addr_start = StartAddress;
        addr_spam = SpammerAddress;
        opt_spammer_on = addr_spam.has_value();
        pnode = pNode;
        if(opt_is_proxy_v1) {

#if !defined(SPAMMER) // see: client\include\client\params.hpp
            // thanks to Solver constructor :-), it has 3 args in this case
            pslv_v1 = std::make_unique<cs::Solver>(pNode, addr_genesis, addr_start);
#else
            // thanks to Solver constructor :-), it has 4 args in this case
            pslv_v1 = std::make_unique<cs::Solver>(pNode, addr_genesis, addr_start, addr_spam.value_or(csdb::Address {}));
#endif

            pws = pslv_v1->m_walletsState.get();
        }
        else {
            pws_inst = std::make_unique<cs::WalletsState>(pNode->getBlockChain());
            // temp decision until solver-1 may be instantiated:
            pws = pws_inst.get();
        }
}

    SolverCore::~SolverCore()
    {
        scheduler.Stop();
        transitions.clear();
    }

    void SolverCore::ExecuteStart(Event start_event)
    {
        if(!is_finished()) {
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: cannot start again, already started");
            }
            return;
        }
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: starting in " << (opt_is_proxy_v1 ? "proxy" : "standalone") << " mode");
        }
        req_stop = false;
        handleTransitions(start_event);
    }

    void SolverCore::finish()
    {
        if(pstate) {
            pstate->off(*pcontext);
        }
        scheduler.RemoveAll();
        tag_state_expired = CallsQueueScheduler::no_tag;
        pstate = std::make_shared<NoState>();
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
        
        if(pstate) {
            pstate->off(*pcontext);
        }
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: switch "
                << (pstate ? pstate->name() : "null")
                << " -> "
                << (pState ? pState->name() : "null"));
        }
        pstate = pState;
        if(!pstate) {
            return;
        }
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
            }, true /*replace if exists*/);
        }
    }

    void SolverCore::handleTransitions(Event evt)
    {
        if(!pstate) {
            // unable to work until initTransitions() called
            return;
        }
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
                LOG_DEBUG("SolverCore: event " << static_cast<int>(evt) << " ignored in state " << pstate->name());
            }
            return;
        }
        setState(it->second);
    }

    bool SolverCore::stateCompleted(Result res)
    {
        if(Consensus::Log) {
            if(Result::Failure == res) {
                LOG_ERROR("SolverCore: error in state " << ( pstate ? pstate->name() : "null"));
            }
        }
        return (Result::Finish == res);
    }

    // Copied methods from solver.v1

    void SolverCore::spawn_next_round(const std::vector<cs::PublicKey>& trusted_nodes)
    {
        //if(accepted_pool.to_binary().size() > 0) {
        //    LOG_ERROR("SolverCore: accepet block is not well-formed (binary represenataion must be empty)");
        //}
 
        LOG_NOTICE("SolverCore: TRUSTED -> WRITER, do write & send block");

          LOG_NOTICE("Node: init next round1");
          // copied from Solver::gotHash():
          cs::Hashes hashes;
          cs::Conveyer& conveyer = cs::Conveyer::instance();
          cs::RoundNumber round = conveyer.currentRoundNumber();

          {
            cs::SharedLock lock(conveyer.sharedMutex());
            for (const auto& element : conveyer.transactionsPacketTable()) {
              hashes.push_back(element.first);
            }
          }

          cs::RoundTable table;
          table.round = ++round;
          table.confidants = trusted_nodes;
          //table.general = mainNode;

          table.hashes = std::move(hashes);
          conveyer.setRound(std::move(table));
          pnode->sendRoundInfo_(conveyer.roundTable());
          //pnode->onRoundStart(conveyer.roundTable());

        // see Solver-1, writeNewBlock() method
        //accepted_pool.set_writer_public_key(csdb::internal::byte_array(public_key.cbegin(), public_key.cend()));
        //auto& bc = pnode->getBlockChain();
        //bc.finishNewBlock(accepted_pool);
        //// see: Solver-1, addTimestampToPool() method
        //accepted_pool.add_user_field(0, std::to_string(
        //    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
        //));
        //// finalize
        //// see Solver-1, prepareBlockForSend() method
        //accepted_pool.set_previous_hash(bc.getLastWrittenHash()); // also set in bc.putBlock()
        //accepted_pool.set_sequence((bc.getLastWrittenSequence()) + 1); // also set in bc.finishNewBlock()
        //accepted_pool.sign(private_key);

        //if(Consensus::Log) {
        //    LOG_NOTICE("SolverCore: defer & send block[" << accepted_pool.sequence() << "] of "
        //        << accepted_pool.transactions_count() << " trans.");
        //    LOG_NOTICE("SolverCore: block previous hash " << accepted_pool.previous_hash().to_string());
        //    LOG_DEBUG("SolverCore: signed with secret which public is "
        //        << cs::Utils::byteStreamToHex((const char *)accepted_pool.writer_public_key().data(), accepted_pool.writer_public_key().size()));
        //}

        //bc.putBlock(accepted_pool/*, true*/); // defer_write
        //cnt_deferred_trans += accepted_pool.transactions_count();

        //csdb::Pool tmp;
        //{
        //    // at this section adding new transactions unavailable
        //    std::lock_guard<std::mutex> lock(trans_mtx);
        //    trans_pool.set_sequence(pnode->getRoundNumber() + 1);
        //    trans_pool.compose();
        //    tmp = trans_pool;
        //    trans_pool = csdb::Pool {};
        //}
        //if(Consensus::Log) {
        //    LOG_NOTICE("SolverCore: send pool [" << tmp.sequence() << "] of "
        //        << tmp.transactions_count() << " trans.");
        //}
        

        //TODO: store transactions sent until they found in future accepted blocks
    }

    void SolverCore::store_received_block(csdb::Pool& p, bool /*defer_write*/)
    {
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: store received block #" << p.sequence() << ", " << p.transactions_count() << " transactions");
        }

        auto& bc = pnode->getBlockChain();

        // see: Solver-1, method Solver::gotBlock()
        if(!bc.onBlockReceived(p/*, defer_write*/)) {
            if(Consensus::Log) {
                LOG_ERROR("SolverCore: block sync required");
            }
            return;
        }

        total_accepted_trans += p.transactions_count();
    }

    bool SolverCore::is_block_deferred() const
    {
        return false; // pnode->getBlockChain().isLastBlockDeferred();
    }

    void SolverCore::flush_deferred_block()
    {
        // if nothing to save deferred_block has zero sequence number
        if(!is_block_deferred()) {
            return;
        }
        //pnode->getBlockChain().writeDeferredBlock();
        total_accepted_trans += cnt_deferred_trans;
        cnt_deferred_trans = 0;
    }

    void SolverCore::drop_deferred_block()
    {
        if(!is_block_deferred()) {
            return;
        }
        if(false /*pnode->getBlockChain().revertLastBlock()*/) {
            //TODO: bc.revertWalletsInPool(deferred_block);
            if(Consensus::Log) {
                LOG_WARN("SolverCore: deferred block dropped, wallets are reverted");
            }
        }
        else {
            if(Consensus::Log) {
                LOG_ERROR("SolverCore: cannot drop deferred block");
            }
            total_accepted_trans += cnt_deferred_trans;
        }
        cnt_deferred_trans = 0;
    }

    void SolverCore::test_outrunning_blocks()
    {
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: test_outrunning_blocks()");
        }
        // retrieve blocks until cache empty or block sequence is broken:
        auto& bc = pnode->getBlockChain();
        while(! outrunning_blocks.empty()) {
            size_t desired_seq = bc.getLastWrittenSequence() + 1;
            const auto oldest = outrunning_blocks.cbegin();
            if(oldest->first < desired_seq) {
                // clear outdated block if it is and select next one:
                if(Consensus::Log) {
                    LOG_NOTICE("SolverCore: remove outdated block #" << oldest->first << " from cache");
                }
                outrunning_blocks.erase(oldest);
            }
            else if(oldest->first == desired_seq) {
                if(Consensus::Log) {
                    LOG_NOTICE("SolverCore: retrieve required block #" << desired_seq << " from cache");
                }
                // retrieve and use block if it is exactly what we need:
                auto& data = outrunning_blocks.at(desired_seq);
                // if state is not set store block also:
                if(desired_seq == cur_round && pstate) {
                    if(stateCompleted(pstate->onBlock(*pcontext, data.first, data.second))) {
                        // do not forget make proper transitions if they are set in our table
                        handleTransitions(Event::Block);
                    }
                }
                else {
                    // store block and remove it from cache
                    store_received_block(data.first, false);
                }
                outrunning_blocks.erase(desired_seq);
            }
            else {
                // stop processing, we have not got required block yet
                if(Consensus::Log) {
                    LOG_DEBUG("SolverCore: nothing to retrieve yet");
                }
                break;
            }
        }
    }

    void SolverCore::gotRoundInfoRequest(uint8_t /*requesterNumber*/)
    {


    }

} // slv2
