#include "SolverCore.h"
#include "SolverContext.h"
#include "CallsQueueScheduler.h"
#include "Consensus.h"
#include "Node.h"
#include "SolverCompat.h"
#include <Solver/WalletsState.h>
#include <Solver/Fee.h>
#include "Generals.h"

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
    constexpr const bool ProxyToOldSolver = true;
    // Special mode: main node serve as collector and writer through all the rounds, other nodes works as normal
    constexpr const bool PermanentNodeRole = false;

    // default (test intended) constructor
    SolverCore::SolverCore()
        // options
        : opt_timeouts_enabled(TimeoutsEnabled)
        , opt_repeat_state_enabled(RepeatStateEnabled)
        , opt_spammer_on(SpammerOn)
        , opt_is_proxy_v1(ProxyToOldSolver)
        , opt_is_permanent_roles(PermanentNodeRole)
        // inner data
        , pcontext(std::make_unique<SolverContext>(*this))
        , tag_state_expired(CallsQueueScheduler::no_tag)
        , req_stop(true)
        , cnt_trusted_desired(Consensus::MinTrustedNodes)
        // consensus data
        , addr_spam(std::nullopt)
        , cur_round(0)
        , pown_hvec(std::make_unique<cs::HashVector>())
        , pfee(std::make_unique<cs::Fee>())
        , last_trans_list_recv(std::numeric_limits<uint64_t>::max())
        , is_bigbang(false)
        // previous solver version instance
        , pslv_v1(nullptr)
        , pnode(nullptr)
        , pws_inst(nullptr)
        , pws(nullptr)
        , pgen_inst(nullptr)
        , pgen(nullptr)
    {
        if(!opt_is_permanent_roles) {
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: use default transition table");
            }
            InitTransitions();
        }
        else {
            if(Consensus::Log) {
                LOG_WARN("SolverCore: opt_permanent_roles is on, so use special transition table");
            }
            InitPermanentTransitions();
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

            pgen = pslv_v1->m_generals.get();
            pws = pslv_v1->walletsState.get();
        }
        else {
            pws_inst = std::make_unique<cs::WalletsState>(pNode->getBlockChain());
            // temp decision until solver-1 may be instantiated:
            pws = pws_inst.get();
            pgen_inst = std::make_unique<cs::Generals>(*pws_inst);
            // temp decision until solver-1 may be instantiated:
            pgen = pgen_inst.get();
        }
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
                LOG_DEBUG("SolverCore: event " << static_cast<int>(evt) << " ignored in state " << pstate->name());
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
        if(cur_round == block_pool.sequence()) {
            // still actual, send it again
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: current block is ready to repeat, do it");
                LOG_NOTICE("SolverCore: sending block #" << block_pool.sequence() << " of " << block_pool.transactions_count() << " transactions");
            }
            pnode->sendBlock(block_pool);
        }
        else {
            // load block and send it
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: current block is out of date, so load stored block to repeat");
            }
            auto& bch = pnode->getBlockChain();
            csdb::Pool p = bch.loadBlock(bch.getLastWrittenHash());
            if(p.is_valid()) {
                if(Consensus::Log) {
                    LOG_NOTICE("SolverCore: sending block #" << p.sequence() << " of " << p.transactions_count() << " transactions");
                }
                pnode->sendBlock(p);
            }
        }
    }

    // Copied methods from solver.v1
    
    void SolverCore::createAndSendNewBlockFrom(csdb::Pool & p)
    {
        pnode->becomeWriter();

        // see Solver-1, writeNewBlock() method
        p.set_writer_public_key(public_key);
        auto& bc = pnode->getBlockChain();
        bc.finishNewBlock(p);
        // see: Solver-1, addTimestampToPool() method
        p.add_user_field(0, std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
        ));
        // finalize
        // see Solver-1, prepareBlockForSend() method
        p.set_sequence((bc.getLastWrittenSequence()) + 1);
        p.sign(private_key);

        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: store & send block #" << p.sequence() << ", " << p.transactions_count() << " transactions");
        }
        pnode->sendBlock(p);
        bc.writeNewBlock(p);
        bc.setGlobalSequence(static_cast<uint32_t>(p.sequence()));
    }

    void SolverCore::storeReceivedBlock(csdb::Pool& p)
    {
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: store received block #" << p.sequence() << ", " << p.transactions_count() << " transactions");
        }
        // see: Solver-1, method Solver::gotBlock()
        if(!pnode->getBlockChain().onBlockReceived(p)) {
            if(Consensus::Log) {
                LOG_ERROR("SolverCore: BlockChain::onBlockReceived() reports an itnernal error");
            }
        }
    }

    size_t SolverCore::flushTransactions()
    {
        // thread-safe with send_wallet_transaction(), suppose to sync with calls from network-related threads
        std::lock_guard<std::mutex> l(trans_mtx);
        //TODO: force  update counter due to possible adding transactions by direct call to push_back(), not to add_transaction() method
        trans_pool.recount();
        size_t tr_cnt = trans_pool.transactions_count();
        if(tr_cnt > 0) {
            //pnode->sendTransaction(std::move(trans_pool)); //vshilkin
            if(Consensus::Log) {
				std::ostringstream os;
				for (const auto& t : trans_pool.transactions()) {
					os << " " << t.innerID();
				}
				LOG_DEBUG("SolverCore: flush" << os.str());
                LOG_DEBUG("SolverCore: " << tr_cnt << " are sent, clear buffer");
            }
            trans_pool = csdb::Pool {};
        }
        else if(Consensus::Log) {
            LOG_DEBUG("SolverCore: no transactions collected, nothing to send");
        }
        return tr_cnt;
    }

    csdb::Pool SolverCore::removeTransactionsWithBadSignatures(const csdb::Pool& p)
    {
        csdb::Pool good;
        BlockChain::WalletData data_to_fetch_pulic_key;
        BlockChain& bc = pnode->getBlockChain();
        for( const auto& tr: p.transactions()) {
            const auto& src = tr.source();
            csdb::internal::byte_array pk;
            if(src.is_wallet_id()) {
                bc.findWalletData(src.wallet_id(), data_to_fetch_pulic_key);
                pk.assign(data_to_fetch_pulic_key.address_.cbegin(), data_to_fetch_pulic_key.address_.cend());
            }
            else {
                const auto& tmpref = src.public_key();
                pk.assign(tmpref.cbegin(), tmpref.cend());
            }
            bool force_permit = (opt_spammer_on && addr_spam.has_value() && pk == addr_spam.value().public_key());
            if(force_permit || tr.verify_signature(pk)) {
                if(Consensus::Log && force_permit) {
                    LOG_WARN("SolverCore: permit drain " << static_cast<int>(tr.amount().to_double()) << " from spammer wallet ignoring check signature");
                }
                good.add_transaction(tr);
            }
        }
        if(Consensus::Log) {
            auto cnt_before = p.transactions_count();
            auto cnt_after = good.transactions_count();
            if(cnt_before != cnt_after) {
                LOG_WARN("SolverCore: " << cnt_before << " transactions filtered to " << cnt_after << " while test signatures");
            }
        }
        return good;
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
                    LOG_NOTICE("SolverCore: remove outdated block #" << desired_seq << " from cache");
                }
                outrunning_blocks.erase(oldest);
            }
            else if(oldest->first == desired_seq) {
                if(Consensus::Log) {
                    LOG_NOTICE("SolverCore: retrieve and use required block #" << desired_seq << " from cache");
                }
                // retrieve and use block if it is exactly what we need:
                auto& data = outrunning_blocks.at(desired_seq);
                if(desired_seq == cur_round) {
                    if(stateCompleted(pstate->onBlock(*pcontext, data.first, data.second))) {
                        // do not forget make proper transitions if they are set in our table
                        handleTransitions(Event::Block);
                    }
                }
                else {
                    // store block and remove it from cache
                    storeReceivedBlock(data.first);
                }
                outrunning_blocks.erase(desired_seq);
            }
            else {
                // stop processing, we have not got required block yet
                if(Consensus::Log) {
                    LOG_DEBUG("SolverCore: nothing to retreive yet");
                }
                break;
            }
        }
    }

    void SolverCore::cache_vector(uint8_t sender, const cs::HashVector& vect)
    {
        //if(vect.is_empty()) {
        //    return;
        //}
        std::vector<VectorVariant>& variants = vector_cache[sender];
        bool update_existsing = false;
        for(auto& v : variants) {
            if(memcmp(&v.first.hash, &vect.hash, sizeof(cs::HashVector)) == 0) {
                v.second++;
                update_existsing = true;
                break;
            }
        }
        if(!update_existsing) {
            variants.push_back(std::make_pair(vect, 1));
        }
    }

    const cs::HashVector * SolverCore::lookup_vector(uint8_t sender) const
    {
        const auto it = vector_cache.find(sender);
        if(it == vector_cache.cend()) {
            return nullptr;
        }
        const auto& variants = it->second;
        const cs::HashVector * ptr = nullptr;
        size_t max_cnt = 0;
        for(const auto& v : variants) {
            if(v.second > max_cnt) {
                ptr = &v.first;
                max_cnt = v.second;
            }
        }
        return ptr;
    }

} // slv2
