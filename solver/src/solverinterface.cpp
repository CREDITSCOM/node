#include <consensus.hpp>
#include <solvercore.hpp>
#include <solvercontext.hpp>
#include <smartcontracts.hpp>


#include <csnode/node.hpp>
#include <csnode/conveyer.hpp>

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
    auto pconnector = pnode->getConnector();
    if(pconnector != nullptr) {
      psmarts->init(pub, pconnector->apiHandler());
    }
    else {
      psmarts->init(pub, nullptr);
    }
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
      const auto& trusted = cs::Conveyer::instance().confidants();
      if(trusted.size() >= ptr->writer) {
        return *(trusted.cbegin() + ptr->writer);
      }
    }
    // TODO: redesign getting ref to persistent object
    return SolverContext::zeroKey;
  }

    void SolverCore::gotHash(csdb::PoolHash&& hash, const cs::PublicKey& sender)
  {
    if(cur_round > 1 + pnode->getBlockChain().getLastSequence()) {
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
    if (pnode != nullptr) {
      auto tmp = cs::Conveyer::instance().currentRoundNumber();
      if (cur_round == tmp) {
        cswarning() << "SolverCore: current round #" << tmp << " restarted (BigBang?)";
      }
      cur_round = tmp;
    }
    else {
      cur_round = 1;
    }

    // as store result of current round:
    if (Consensus::Log) {
      csdebug() << "SolverCore: clear all stored round data (block hashes, stages-1..3)";
    }

    recv_hash.clear();
    stageOneStorage.clear();
    stageTwoStorage.clear();
    stageThreeStorage.clear();
    trueStageThreeStorage.clear();
    trusted_candidates.clear();

    if (!pstate) {
      return;
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

    if (stateCompleted(pstate->onRoundTable(*pcontext, cur_round))) {
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
    csdebug() << "SolverCore: <-- stage-1 [" << (int) stage.sender << "] = " << stageOneStorage.size();

    if(!pstate) {
      return;
    }
    if(stateCompleted(pstate->onStage1(*pcontext, stage))) {
      handleTransitions(Event::Stage1Enough);
    }
  }

  void SolverCore::gotStageOneRequest(uint8_t requester, uint8_t required)
  {
    csdebug() << "SolverCore: [" << (int) requester << "] asks for stage-1 of [" << (int) required << "]";
    const auto ptr = find_stage1(required);
    if(ptr != nullptr) {
      pnode->sendStageReply(ptr->sender, ptr->signature, MsgTypes::FirstStage , requester);
    }
  }

  void SolverCore::gotStageTwoRequest(uint8_t requester, uint8_t required)
  {
    csdebug() << "SolverCore: [" << (int) requester << "] asks for stage-2 of [" << (int) required << "]";
    const auto ptr = find_stage2(required);
    if(ptr != nullptr) {
      pnode->sendStageReply(ptr->sender, ptr->signature, MsgTypes::SecondStage, requester);
    }
  }

  void SolverCore::gotStageThreeRequest(uint8_t requester, uint8_t required)
  {
    csdebug() << "SolverCore: [" << (int) requester << "] asks for stage-3 of [" << (int) required << "]";
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
    csdebug() << "SolverCore: <-- stage-2 [" << (int) stage.sender << "] = " << stageTwoStorage.size();

    if(!pstate) {
      return;
    }
    if(stateCompleted(pstate->onStage2(*pcontext, stage))) {
      handleTransitions(Event::Stage2Enough);
    }
  }

  void SolverCore::printStage3(const cs::StageThree& stage) {
      std::string realTrustedString;
      for (auto& i : stage.realTrustedMask) {
        realTrustedString = realTrustedString + "[" + std::to_string((int)i) + "] ";
      }
      csdebug() << "     SENDER = " << (int)stage.sender << ", WRITER = " << (int)stage.writer << ", RealTrusted = " << realTrustedString;
      csdebug() << "     BlockHash = " << cs::Utils::byteStreamToHex(stage.blockHash.data(), stage.blockHash.size()); 
      csdebug() << "     BlockSign = " << cs::Utils::byteStreamToHex(stage.blockSignature.data(), stage.blockSignature.size());
      csdebug() << "     RoundHash = " << cs::Utils::byteStreamToHex(stage.roundHash.data(), stage.roundHash.size());
      csdebug() << "     RoundSign = " << cs::Utils::byteStreamToHex(stage.roundSignature.data(), stage.roundSignature.size());

  }

  void SolverCore::gotStageThree(const cs::StageThree& stage, const uint8_t flagg)
  {
    const cs::Conveyer& conveyer = cs::Conveyer::instance();
    if(find_stage3(stage.sender) != nullptr) {
      // duplicated
      return;
    }
    //if()
    switch (flagg) {
      case 0:
        stageThreeStorage.push_back(stage);
        break;
      case 1:
        for (auto& st : stageThreeStorage) {
//          csdebug() << "OUR:";
//          printStage3(stage);
//          csdebug() << "GOT:";
//          st.print();
          //TODO: change the routine of pool signing
          bool blockSignaturesOk = cscrypto::verifySignature(st.blockSignature, conveyer.confidantByIndex(st.sender), stage.blockHash.data(), stage.blockHash.size());
          if (!blockSignaturesOk) {
            csdebug() << "Block Signatures are not ok";
          }
          bool roundSignaturesOk = cscrypto::verifySignature(st.roundSignature, conveyer.confidantByIndex(st.sender), stage.roundHash.data(), stage.roundHash.size());
          if (!roundSignaturesOk) {
            csdebug() << "Round Signatures are not ok";
          }
          bool realTrustedOk = (st.realTrustedMask == stage.realTrustedMask);
          if (!realTrustedOk) {
            csdebug() << "Real Trusted are not ok";
          }
          bool writerOk = st.writer == stage.writer;
          if (!writerOk) {
            csdebug() << "Writer is not ok";
          }
          if (blockSignaturesOk && roundSignaturesOk && realTrustedOk && writerOk) {
            trueStageThreeStorage.push_back(st);
            pnode->addRoundSignature(st);
            csdebug() << "Stage3 [" << (int)st.sender << "] - signatures are OK";
          }
        }
        trueStageThreeStorage.push_back(stage);
        pnode->addRoundSignature(stage);
        stageThreeStorage.push_back(stage);
        break;
      case 2:
        const auto st = find_stage3(pnode->getConfidantNumber());
//        csdebug() << "OUR:";
//        st->print();
//        csdebug() << "GOT:";
//        printStage3(stage);
        bool blockSignaturesOk = cscrypto::verifySignature(stage.blockSignature, conveyer.confidantByIndex(stage.sender), st->blockHash.data(), st->blockHash.size());
        if(!blockSignaturesOk) {
          csdebug() << "Block Signatures are not ok";
        }
        bool roundSignaturesOk = cscrypto::verifySignature(stage.roundSignature, conveyer.confidantByIndex(stage.sender), st->roundHash.data(), st->roundHash.size());
        if (!roundSignaturesOk) {
          csdebug() << "Round Signatures are not ok";
        }
        bool realTrustedOk = (st->realTrustedMask == stage.realTrustedMask);
        if (!realTrustedOk) {
          csdebug() << "Real Trusted are not ok";
        }
        bool writerOk = st->writer == stage.writer;
        if (!writerOk) {
          csdebug() << "Writer is not ok";
        }
        if (blockSignaturesOk && roundSignaturesOk && realTrustedOk && writerOk) {
          trueStageThreeStorage.push_back(stage);
          pnode->addRoundSignature(stage);
          csdebug() << "Stage3 [" << (int)stage.sender << "] - signatures are OK";
        }
        stageThreeStorage.push_back(stage);
      break;
    }

    csdebug() << "SolverCore: <-- stage-3 [" << (int) stage.sender << "] = " << stageThreeStorage.size() << " : " << trueStageThreeStorage.size();

    if(!pstate) {
      return;
    }
    if(stateCompleted(pstate->onStage3(*pcontext, stage))) {
      handleTransitions(Event::Stage3Enough);
    }
  }

  size_t SolverCore::trueStagesThree() {
    return trueStageThreeStorage.size();
  }
  

  void SolverCore::send_wallet_transaction(const csdb::Transaction& tr)
  {
    //DEBUG:
#if defined(DEBUG_SMARTS)
    if(SmartContracts::is_smart_contract(tr)) {
      psmarts->force_execution = true;
    }
#endif
    if(psmarts->test_smart_contract_emits(tr)) {
      // avoid pass to conveyer until execution of emitter contract has finished
      csdebug() << "SolverCore: smart contract emits transaction";
      return;
    }
    cs::Conveyer::instance().addTransaction(tr);
  }

  void SolverCore::gotRoundInfoRequest(const cs::PublicKey& requester, cs::RoundNumber requester_round)
  {
    csdebug() << "SolverCore: got round info request from "
      << cs::Utils::byteStreamToHex(requester.data(), requester.size());

    if(requester_round == cur_round) {
      const auto ptr = /*cur_round == 10 ? nullptr :*/ find_stage3(pnode->getConfidantNumber());
      if(ptr != nullptr) {
        if(ptr->sender == ptr->writer) {
          if(pnode->tryResendRoundTable(requester, cur_round)) {
            csdebug() << "SolverCore: re-send full round info #" << cur_round << " completed";
            return;
          }
        }
      }
      csdebug() << "SolverCore: also on the same round, inform cannot help with";
      pnode->sendRoundTableReply(requester, false);
    }
    else if(requester_round < cur_round) {
      for(const auto& node : cs::Conveyer::instance().confidants()) {
        if(requester == node) {
          if(pnode->tryResendRoundTable(requester, cur_round)) {
            csdebug() << "SolverCore: requester is trusted next round, supply it with round info";
          }
          else {
            csdebug() << "SolverCore: try but cannot send full round info";
          }
          return;
        }
      }
      csdebug() << "SolverCore: inform requester next round has come and it is not in trusted list";
      pnode->sendRoundTableReply(requester, true);
    }
    else {
      // requester_round > cur_round, cannot help with!
      csdebug() << "SolverCore: cannot help with outrunning round info";
    }
  }

  void SolverCore::gotRoundInfoReply(bool next_round_started, const cs::PublicKey& /*respondent*/)
  {
    if (next_round_started) {
      csdebug() << "SolverCore: round info reply means next round started, and I am not trusted node. Waiting next round";
      return;
    }
    csdebug() << "SolverCore: round info reply means next round is not started, become writer";
    handleTransitions(SolverCore::Event::SetWriter);
  }
  
}  // namespace cs
