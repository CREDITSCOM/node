#include <SolverCore.h>
#include <Consensus.h>

#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <solver/solver.hpp>
#pragma warning(pop)

#include <solver/Fee.h>
#include <csdb/currency.h>
#include <lib/system/logger.hpp>

#include <chrono>

namespace slv2
{

    void SolverCore::setKeysPair(const cs::PublicKey& pub, const cs::PrivateKey& priv)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->setKeysPair(pub, priv);
        }
        public_key = pub;
        private_key = priv;
    }

    void SolverCore::runSpammer()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->runSpammer();
            return;
        }
        if(!pspam) {
            if(Consensus::Log) {
                LOG_WARN("SolverCore: starting transaction spammer");
            }
            pspam = std::make_unique<cs::Spammer>();
            pspam->StartSpamming(*pnode);
        }
    }

    void SolverCore::countFeesInPool(csdb::Pool * pool)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->countFeesInPool(pool);
            return;
        }
        this->pfee->CountFeesInPool(pnode->getBlockChain(), pool);
    }

    void SolverCore::gotRound(cs::RoundNumber rNum)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->gotRound();
            return;
        }
        
        // previous solver implementation calls to runConsensus method() here
        // perform similar actions, but only in proper state (TrustedStage1State for now)
        
        // clear data
        markUntrusted.fill(0);

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onSyncTransactions(*pcontext, rNum))) {
            handleTransitions(Event::Transactions);
        }
    }

    const cs::PublicKey& SolverCore::getWriterPublicKey() const
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            // temporary workaround of return reference to rvalue
            static cs::PublicKey persist_obj = cs::PublicKey {};
            persist_obj = pslv_v1->writerPublicKey();
            return persist_obj;
        }

        // Previous solver returns confidant key with index equal result of takeDecision() method.
        // As analogue, found writer's index in stage3 if exists, otherwise return empty object as prev. solver does
        auto ptr = find_stage3(pnode->getConfidantNumber());
        if(ptr != nullptr) {
            const auto& trusted = cs::Conveyer::instance().roundTable().confidants;
            if(trusted.size() >= ptr->writer) {
                return *(trusted.cbegin() + ptr->writer);
            }
        }
        // TODO: redesign getting ref to persistent object
        static cs::PublicKey empty {};
        return empty;
    }

    void SolverCore::addInitialBalance()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->addInitialBalance();
            return;
        }
    }

    void SolverCore::setBigBangStatus(bool status)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->setBigBangStatus(status);
            return;
        }

        is_bigbang = status;

        if(!pstate) {
            return;
        }
        if(status) {
            handleTransitions(Event::BigBang);
        }
    }

    void SolverCore::gotTransaction(const csdb::Transaction& trans)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            csdb::Transaction tmp = trans;
            pslv_v1->gotTransaction(std::move(tmp));
            return;
        }

        if(!pstate) {
            return;
        }
        // produces too much output:
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: got transaction " << trans.innerID() << " from " << trans.source().to_string());
        }
        if(stateCompleted(pstate->onTransaction(*pcontext, trans))) {
            handleTransitions(Event::Transactions);
        }
    }

    void SolverCore::gotVector(const cs::HashVector& vect)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            cs::HashVector tmp = vect;
            pslv_v1->gotVector(std::move(tmp));
            return;
        }

        if(Consensus::Log) {
            LOG_ERROR("SolverCore: method gotVector() is obsolete in current version");
        }
    }

    void SolverCore::gotMatrix(cs::HashMatrix&& matr)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            cs::HashMatrix tmp = matr;
            pslv_v1->gotMatrix(std::move(tmp));
            return;
        }

        if(Consensus::Log) {
            LOG_ERROR("SolverCore: method gotMatrix() is obsolete in current version");
        }
    }

    void SolverCore::gotBlock(csdb::Pool&& p, const cs::PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            csdb::Pool tmp = p;
            //TODO: gotBlock_V3() required
            pslv_v1->gotBlock(std::move(tmp), sender);
            return;
        }

        // solver-1 logic: clear bigbang status upon block receive
        is_bigbang = false;

        // solver-1: caching, actually duplicates caching implemented in Node::getBlock()
        csdb::Pool::sequence_t desired_seq = pnode->getBlockChain().getLastWrittenSequence() + 1;
        if(p.sequence() != desired_seq) {
            gotIncorrectBlock(std::move(p), sender);
            return;
        }

        if(!pstate) {
            return;
        }
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: gotBlock()");
        }
        if(stateCompleted(pstate->onBlock(*pcontext, p, sender))) {
            handleTransitions(Event::Block);
        }
        // makes subsequent calls to pstate->onBlock() if find appropriate next blocks in cache:
        test_outrunning_blocks();
    }

    void SolverCore::gotBlockRequest(const csdb::PoolHash& p_hash, const cs::PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            csdb::PoolHash tmp = p_hash;
            pslv_v1->gotBlockRequest(std::move(tmp), sender);
            return;
        }

        std::ostringstream os;
        os << "SolverCore: got request for block, ";
        // state does not take part
        if(pnode != nullptr) {
            csdb::Pool p = pnode->getBlockChain().loadBlock(p_hash);
            if(p.is_valid()) {
                os << "[" << p.sequence() << "] found, sending";
//                pnode->sendBlockReply(p, sender);
            }
            else {
                os << "not found";
            }
        }
        else {
            os << "cannot handle";
        }
        if(Consensus::Log) {
            LOG_EVENT(os.str());
        }
    }

    void SolverCore::gotBlockReply(csdb::Pool& p)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->gotBlockReply(std::move(p));
            return;
        }
        if(!pstate) {
            return;
        }
        // "uncache" stored hashes if any
        if(!recv_hash.empty()) {
            if(cur_round - pnode->getBlockChain().getLastWrittenSequence() == 1) {
                for(const auto& hash_sender : recv_hash) {
                    if(stateCompleted(pstate->onHash(*pcontext, hash_sender.first, hash_sender.second))) {
                        handleTransitions(Event::Hashes);
                    }
                }
            }
        }
    }

    void SolverCore::gotHash(csdb::PoolHash&& hash, const cs::PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->gotHash(std::move(hash), sender);
            return;
        }

        csdb::Pool::sequence_t delta = cur_round - pnode->getBlockChain().getLastWrittenSequence();
        if(delta > 1) {
            recv_hash.push_back(std::make_pair<>(hash, sender));
            csdebug() << "SolverCore: cache hash until last block ready";
            return;
        }

        if(!pstate) {
            return;
        }

        if(stateCompleted(pstate->onHash(*pcontext, hash, sender))) {
            handleTransitions(Event::Hashes);
        }
    }

    void SolverCore::gotIncorrectBlock(csdb::Pool&& p, const cs::PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->gotIncorrectBlock(std::move(p), sender);
            return;
        }

        // store outrunning block for future using
        const auto seq = p.sequence();

        // test proper sequence
        if(pnode->getBlockChain().getLastWrittenSequence() >= seq) {
            if(Consensus::Log) {
                LOG_DEBUG("SolverCore: <-- block [" << seq << "] of " << p.transactions_count() << ", outdated ignored");
            }
            return;
        }

        if(outrunning_blocks.count(seq) == 0) {
            outrunning_blocks [seq] = std::make_pair(p, sender);
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: <-- block [" << seq << "] of " << p.transactions_count() << ", outrunning cached");
            }
        }
        else {
            if(Consensus::Log) {
                LOG_DEBUG("SolverCore: <-- block [" << seq << "] of " << p.transactions_count() << ", duplicated ignored");
            }
        }
    }

    void SolverCore::tmpStorageProcessing()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->tmpStorageProcessing();
            return;
        }

        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: tmpStorageProcessing()");
        }
        test_outrunning_blocks();
    }

    void SolverCore::gotFreeSyncroBlock(csdb::Pool&& p)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->gotFreeSyncroBlock(std::move(p));
            return;
        }

        gotIncorrectBlock(std::move(p), cs::PublicKey {});
    }

    void SolverCore::rndStorageProcessing()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->rndStorageProcessing();
            return;
        }
        
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: rndStorageProcessing()");
        }

        test_outrunning_blocks();
    }

    void SolverCore::beforeNextRound()
    {
        if(!pstate) {
            return;
        }
        pstate->onRoundEnd(*pcontext, is_bigbang);
    }

    void SolverCore::nextRound()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->nextRound();
            return;
        }

        // minimal statistics, skip 0 & 1 rounds because of possibility extra timeouts
        if(cur_round < 2) {
            t_start_ms = std::chrono::steady_clock::now();
            total_duration_ms = 0;
        }
        else {
            using namespace std::chrono;
            auto new_duration_ms = duration_cast<milliseconds>(steady_clock::now() - t_start_ms).count();
            auto last_round_ms = new_duration_ms - total_duration_ms;
            total_duration_ms = new_duration_ms;
            auto ave_round_ms = total_duration_ms / cur_round;

            //TODO: use more intelligent output formatting
            std::ostringstream os;
            constexpr size_t in_minutes = 5 * 60 * 1000;
            constexpr size_t in_seconds = 10 * 1000;
            os << "SolverCore: last round ";
            if(last_round_ms > in_minutes) {
                os << "> " << last_round_ms / 60000 << "min";
            }
            else if(last_round_ms > in_seconds) {
                os << "> " << last_round_ms / 1000 << "sec";
            }
            else {
                os << last_round_ms << "ms";
            }
            os << ", average round ";
            if(ave_round_ms > in_seconds) {
                os << "> " << ave_round_ms / 1000 << "sec";
            }
            else {
                os << ave_round_ms << "ms";
            }
            os << ", " << total_recv_trans << " viewed trans., " << total_accepted_trans << " stored trans.";
            LOG_NOTICE(os.str());
        }

        if(pnode != nullptr) {
            auto tmp = pnode->getRoundNumber();
            if(cur_round == tmp) {
                return;
            }
            cur_round = tmp;
        }
        else {
            cur_round = 1;
        }

        // as store result of current round:
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: clear all stored senders (vectors, matrices, hashes)");
        }

        recv_hash.clear();
        stageOneStorage.clear();
        stageTwoStorage.clear();
        stageThreeStorage.clear();

        if(!pstate) {
            return;
        }

        // update desired count of trusted nodes
        size_t cnt_trusted = cs::Conveyer::instance().roundTable().confidants.size();
        if(cnt_trusted > cnt_trusted_desired) {
            cnt_trusted_desired = cnt_trusted;
        }

        auto desired_seq = pnode->getBlockChain().getLastWrittenSequence() + 2;
        if(desired_seq < cur_round) {
            // empty args requests exactly what we need:
//            pnode->sendBlockRequest();
        }

        if(stateCompleted(pstate->onRoundTable(*pcontext, static_cast<uint32_t>(cur_round)))) {
            handleTransitions(Event::RoundTable);
        }

        if(1 == cur_round) {
            scheduler.InsertOnce(Consensus::T_round, [this]() {
                pnode->sendHash_V3(1);
                //gotTransactionList_V3(std::move(csdb::Pool{}));
            });
        }
        //TODO: not good solution, to reproduce solver-1 logic only:
        else if(is_bigbang) {
            scheduler.InsertOnce(Consensus::T_coll_trans, [this]() {
                csdb::Pool tmp {};
                tmp.set_sequence(cur_round - 1);
                //gotTransactionList_V3(std::move(tmp));
            });
        }
    }

    void SolverCore::gotStageOne(const cs::StageOne & stage)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            if(Consensus::Log) {
                LOG_ERROR("SolverCore: method gotStageOne() is not implemented in proxied solver object");
            }
            return;
        }

        if(find_stage1(stage.sender) != nullptr) {
            // duplicated
            return;
        }

        stageOneStorage.push_back(stage);
        LOG_NOTICE("SolverCore: <-- stage-1 [" << (int) stage.sender << "] = " << stageOneStorage.size());

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onStage1(*pcontext, stage))) {
            handleTransitions(Event::Stage1Enough);
        }
    }

    void SolverCore::gotStageOneRequest(uint8_t requester, uint8_t required)
    {
        LOG_NOTICE("SolverCore: [" << (int) requester << "] asks for stage-1 of [" << (int) required << "]");
        const auto ptr = find_stage1(required);
        if(ptr != nullptr) {
            pnode->sendStageOneReply(*ptr, requester);
        }
    }
    
    void SolverCore::gotStageTwoRequest(uint8_t requester, uint8_t required)
    {
        LOG_NOTICE("SolverCore: [" << (int) requester << "] asks for stage-2 of [" << (int) required << "]");
        const auto ptr = find_stage2(required);
        if(ptr != nullptr) {
            pnode->sendStageTwoReply(*ptr, requester);
        }
    }
    
    void SolverCore::gotStageThreeRequest(uint8_t requester, uint8_t required)
    {
        LOG_NOTICE("SolverCore: [" << (int) requester << "] asks for stage-3 of [" << (int) required << "]");
        const auto ptr = find_stage3(required);
        if(ptr != nullptr) {
            pnode->sendStageThreeReply(*ptr, requester);
        }
    }

    void SolverCore::gotStageTwo(const cs::StageTwo & stage)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            if(Consensus::Log) {
                LOG_ERROR("SolverCore: method gotStageTwo() is not implemented in proxied solver object");
            }
            return;
        }

        if(find_stage2(stage.sender) != nullptr) {
            // duplicated
            return;
        }

        stageTwoStorage.push_back(stage);
        LOG_NOTICE("SolverCore: <-- stage-2 [" << (int) stage.sender << "] = " << stageTwoStorage.size());

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onStage2(*pcontext, stage))) {
            handleTransitions(Event::Stage2Enough);
        }
    }

    void SolverCore::gotStageThree(const cs::StageThree & stage)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            if(Consensus::Log) {
                LOG_ERROR("SolverCore: method gotStageThree() is not implemented in proxied solver object");
            }
            return;
        }

        if(find_stage3(stage.sender) != nullptr) {
            // duplicated
            return;
        }

        stageThreeStorage.push_back(stage);
        LOG_NOTICE("SolverCore: <-- stage-3 [" << (int) stage.sender << "] = " << stageThreeStorage.size());

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onStage3(*pcontext, stage))) {
            handleTransitions(Event::Stage3Enough);
        }
    }

    void SolverCore::send_wallet_transaction(const csdb::Transaction& tr)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->send_wallet_transaction(tr);
            return;
        }
        cs::Conveyer::instance().addTransaction(tr);
    }

    csdb::Pool::sequence_t SolverCore::getNextMissingBlock(const uint32_t starting_after) const
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            return pslv_v1->getNextMissingBlock(starting_after);
        }

        for(csdb::Pool::sequence_t b = starting_after + 1; b < cur_round; ++b) {
            if(outrunning_blocks.count(b) > 0) {
                continue;
            }
            return b;
        }
        return 0;
    }

    csdb::Pool::sequence_t SolverCore::getCountCahchedBlocks(csdb::Pool::sequence_t starting_after, csdb::Pool::sequence_t end) const
    {
        if(outrunning_blocks.empty()) {
            return 0;
        }
        // it: a pass-through iterator for both while() blocks
        auto it = outrunning_blocks.cbegin();
        // skip outdated blocks if any
        while(it->first <= starting_after) {
            if(++it == outrunning_blocks.cend()) {
                return 0;
            }
        }
        // count useful cached blocks
        csdb::Pool::sequence_t cnt = 0;
        while(it->first <= end) {
            ++cnt;
            if(++it == outrunning_blocks.cend()) {
                break;
            }
        }
        return cnt;
    }

    void SolverCore::gotRoundInfoRequest(const cs::PublicKey& requester, cs::RoundNumber requester_round)
    {
        cslog() << "SolverCore: got request for round info from " << cs::Utils::byteStreamToHex(requester.data(), requester.size());

        if(requester_round == cur_round) {
            const auto ptr = /*cur_round == 10 ? nullptr :*/ find_stage3(pnode->getConfidantNumber());
            if(ptr != nullptr) {
                if(ptr->sender == ptr->writer) {
                    if(pnode->tryResendRoundInfo(requester, (cs::RoundNumber) cur_round)) {
                        cslog() << "SolverCore: re-send full round info #" << cur_round;
                        return;
                    }
                }
            }
            cslog() << "SolverCore: also on the same round, inform cannot help with";
            pnode->sendRoundInfoReply(requester, false);
        }
        else if(requester_round < cur_round) {
            for(const auto& node : pnode->confidants()) {
                if(requester == node) {
                    if(pnode->tryResendRoundInfo(requester, (cs::RoundNumber) cur_round)) {
                        cslog() << "SolverCore: requester is trusted next round, supply it with round info";
                        return;
                    }
                    cslog() << "SolverCore: try but cannot send full round info";
                    break;
                }
            }
            cslog() << "SolverCore: inform requester next round has come";
            pnode->sendRoundInfoReply(requester, true);
        }
        else {
            // requester_round > cur_round, cannot help with!
            cslog() << "SolverCore: cannot help with outrunning round info";
        }
    }

    void SolverCore::gotRoundInfoReply(bool next_round_started, const cs::PublicKey& /*respondent*/)
    {
        if(next_round_started) {
            cslog() << "SolverCore: round info reply means next round started, and I am not trusted node. Waiting next round";
            return;
        }
        cswarning() << "SolverCore: round info reply means next round is not started, become writer in 2 sec";
        size_t stored_round = cur_round;
        scheduler.InsertOnce(1000, [this, stored_round]() {
            if(stored_round == cur_round) {
                // still did not receive next round info - become writer
                handleTransitions(SolverCore::Event::SetWriter);
            }
        }, true);
        
    }

} // slv2
