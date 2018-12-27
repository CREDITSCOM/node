#include <consensus.hpp>
#include <solvercore.hpp>
#include <solvercontext.hpp>
#include <smartcontracts.hpp>

#pragma warning(push)
#pragma warning(disable : 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)

#include <csdb/currency.hpp>
#include <lib/system/logger.hpp>
#include <csnode/fee.hpp>

#include <chrono>

namespace cs
{

  void SolverCore::setKeysPair(const cs::PublicKey& pub, const cs::PrivateKey& priv)
  {
    public_key = pub;
    private_key = priv;
    psmarts->init(pub, pnode->getConnector().apiHandler());
  }

  void SolverCore::gotConveyerSync(cs::RoundNumber rNum)
  {
    // clear data
    markUntrusted.fill(0);

    if(!pstate) {
      return;
    }
    if(stateCompleted(pstate->onSyncTransactions(*pcontext, rNum))) {
      handleTransitions(Event::Transactions);
    }

    // restore possibly cached hashes from other nodes
    // this is actual if conveyer has just stored last required block
    if(!recv_hash.empty() && cur_round == rNum) {
      for(const auto& item : recv_hash) {
        if(stateCompleted(pstate->onHash(*pcontext, item.first, item.second))) {
          handleTransitions(Event::Hashes);
        }
      }
    }
  }

  const cs::PublicKey& SolverCore::getWriterPublicKey() const
  {
    // Previous solver returns confidant key with index equal result of takeDecision() method.
    // As analogue, found writer's index in stage3 if exists, otherwise return empty object as prev. solver does
    auto ptr = find_stage3(pnode->getConfidantNumber());
    if(ptr != nullptr) {
      const auto& trusted = cs::Conveyer::instance().currentRoundTable().confidants;
      if(trusted.size() >= ptr->writer) {
        return *(trusted.cbegin() + ptr->writer);
      }
    }
    // TODO: redesign getting ref to persistent object
    return SolverContext::zeroKey;
  }

  void SolverCore::gotBigBang()
  {
    // in case of bigbang resend all info we have got
    // assume normal (ordinary) nodes does not store stages
    const auto own_num = pnode->getConfidantNumber();
    const auto pstage1 = find_stage1(own_num);
    if(pstage1 != nullptr) {
      cslog() << "SolverCore: resend stage-1 after BigBang";
      pnode->sendStageOne(*pstage1);
    }
    else {
      cslog() << "SolverCore: stage-1 not ready to re-send after BigBang";
    }
    const auto pstage2 = find_stage2(own_num);
    if(pstage2 != nullptr) {
      cslog() << "SolverCore: resend stage-2 after BigBang";
      pnode->sendStageTwo(*pstage2);
    }
    else {
      cslog() << "SolverCore: stage-2 not ready to re-send after BigBang";
    }
    const auto pstage3 = find_stage3(own_num);
    if(pstage3 != nullptr) {
      cslog() << "SolverCore: resend stage-3 after BigBang";
      pnode->sendStageThree(*pstage3);
    }
    else {
      cslog() << "SolverCore: stage-3 not ready yet to re-send after BigBang";
    }
  }

  void SolverCore::gotHash(csdb::PoolHash&& hash, const cs::PublicKey& sender)
  {
    cs::Sequence delta = cur_round - pnode->getBlockChain().getLastSequence();
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

  void SolverCore::beforeNextRound()
  {
    if(!pstate) {
      return;
    }
    pstate->onRoundEnd(*pcontext, false /*is_bigbang*/);
  }

  void SolverCore::nextRound()
  {
    if(pnode != nullptr) {
      auto tmp = pnode->getRoundNumber();
      if(cur_round == tmp) {
        cswarning() << "SolverCore: current round #" << tmp << " restarted (BigBang?)";
      }
      cur_round = tmp;
    }
    else {
      cur_round = 1;
    }

    // as store result of current round:
    if(Consensus::Log) {
      LOG_DEBUG("SolverCore: clear all stored round data (block hashes, stages-1..3)");
    }

    recv_hash.clear();
    stageOneStorage.clear();
    stageTwoStorage.clear();
    stageThreeStorage.clear();
    trusted_candidates.clear();

    if(!pstate) {
      return;
    }

    // update desired count of trusted nodes
    size_t cnt_trusted = cs::Conveyer::instance().currentRoundTable().confidants.size();
    if(cnt_trusted > cnt_trusted_desired) {
      cnt_trusted_desired = cnt_trusted;
    }

    // start timeout tracking
    //auto round = cur_round;
    //track_next_round.start(
    //  scheduler,
    //  Consensus::PostConsensusTimeout,
    //  [this, round]() {
    //    if(this->cur_round == round) {
    //      // round have not been changed yet
    //      cswarning() << "SolverCore: request next round info due to timeout " << Consensus::PostConsensusTimeout / 1000 << " sec";
    //      pnode->sendNextRoundRequest();
    //    }
    //  },
    //  true /*replace exisiting*/);

    if(stateCompleted(pstate->onRoundTable(*pcontext, cur_round))) {
      handleTransitions(Event::RoundTable);
    }
  }

  void SolverCore::gotStageOne(const cs::StageOne& stage)
  {
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
      pnode->sendStageReply(ptr->sender,ptr->signature, MsgTypes::FirstStage , requester);
    }
  }

  void SolverCore::gotStageTwoRequest(uint8_t requester, uint8_t required)
  {
    LOG_NOTICE("SolverCore: [" << (int) requester << "] asks for stage-2 of [" << (int) required << "]");
    const auto ptr = find_stage2(required);
    if(ptr != nullptr) {
      pnode->sendStageReply(ptr->sender, ptr->signature, MsgTypes::SecondStage, requester);
    }
  }

  void SolverCore::gotStageThreeRequest(uint8_t requester, uint8_t required)
  {
    LOG_NOTICE("SolverCore: [" << (int) requester << "] asks for stage-3 of [" << (int) required << "]");
    const auto ptr = find_stage3(required);
    if(ptr != nullptr) {
      pnode->sendStageReply(ptr->sender, ptr->signature, MsgTypes::ThirdStage, requester);
    }
  }

  void SolverCore::gotStageTwo(const cs::StageTwo& stage)
  {
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

  void SolverCore::gotStageThree(const cs::StageThree& stage)
  {
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
    //DEBUG:
#if defined(DEBUG_SMARTS)
    if(SmartContracts::is_smart_contract(tr)) {
      psmarts->force_execution = true;
    }
#endif
    csmeta(cslog) << ": trx: src " << tr.source().to_string() << ", tgt " << tr.target().to_string();
    if(psmarts->test_smart_contract_emits(tr)) {
      // avoid pass to conveyer until execution of emitter contract has finished
      cslog() << "SolverCore: running smart contract emits transaction";
      return;
    }
    cs::Conveyer::instance().addTransaction(tr);
  }

  void SolverCore::gotSmartContractEvent(const csdb::Pool block, size_t trx_idx)
  {
    if(trx_idx >= block.transactions_count()) {
      cserror() << "SolverCore: incorrect transaction index related to smart contract";
      return;
    }
    csdb::Transaction tr = * (block.transactions().cbegin() + trx_idx);
    if(!SmartContracts::is_smart_contract(tr)) {
      cserror() << "SolverCore: incorrect transaction type related to smart contract";
      return;
    }
    // dispatch transaction by its type
    bool is_deploy = psmarts->is_deploy(tr);
    bool is_start = is_deploy ? false : psmarts->is_start(tr);
    if(is_deploy || is_start) {
      if(is_deploy) {
        csdebug() << "SolverCore: smart contract is deployed, enqueue it for execution";
      }
      else {
        csdebug() << "SolverCore: smart contract is started, enqueue it for execution";
      }
      psmarts->enqueue(block, trx_idx);
    }
    else if(psmarts->is_new_state(tr)) {
      csdebug() << "SolverCore: smart contract is executed, state updated with new one";
      psmarts->on_completed(block, trx_idx);
    }
    
  }

  void SolverCore::gotRoundInfoRequest(const cs::PublicKey& requester, cs::RoundNumber requester_round)
  {
    cslog() << "SolverCore: got round info request from "
      << cs::Utils::byteStreamToHex(requester.data(), requester.size());

    if(requester_round == cur_round) {
      const auto ptr = /*cur_round == 10 ? nullptr :*/ find_stage3(pnode->getConfidantNumber());
      if(ptr != nullptr) {
        if(ptr->sender == ptr->writer) {
          if(pnode->tryResendRoundTable(requester, cur_round)) {
            cslog() << "SolverCore: re-send full round info #" << cur_round << " completed";
            return;
          }
        }
      }
      cslog() << "SolverCore: also on the same round, inform cannot help with";
      pnode->sendRoundTableReply(requester, false);
    }
    else if(requester_round < cur_round) {
      for(const auto& node : pnode->confidants()) {
        if(requester == node) {
          if(pnode->tryResendRoundTable(requester, cur_round)) {
            cslog() << "SolverCore: requester is trusted next round, supply it with round info";
          }
          else {
            cslog() << "SolverCore: try but cannot send full round info";
          }
          return;
        }
      }
      cslog() << "SolverCore: inform requester next round has come and it is not in trusted list";
      pnode->sendRoundTableReply(requester, true);
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
    cswarning() << "SolverCore: round info reply means next round is not started, become writer in " << Consensus::T_round / 1000U << " sec";
    cserror() << "SolverCore: re-assign writer node to me";
    handleTransitions(SolverCore::Event::SetWriter);
  }
  
}  // namespace cs
