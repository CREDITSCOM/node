#include <consensus.hpp>
#include <solvercore.hpp>
#include <solvercontext.hpp>
#include <smartcontracts.hpp>


#include <csnode/node.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/fee.hpp>

#include <csdb/currency.hpp>
#include <lib/system/logger.hpp>

#include <chrono>

namespace cs {
void SolverCore::setKeysPair(const cs::PublicKey& pub, const cs::PrivateKey& priv) {
  public_key = pub;
  private_key = priv;

  auto pconnector = pnode->getConnector();

  if (pconnector != nullptr) {
    psmarts->init(pub, pconnector->apiHandler());
  }
  else {
    psmarts->init(pub, nullptr);
  }
}

void SolverCore::gotConveyerSync(cs::RoundNumber rNum) {
  // clear data
  markUntrusted.fill(0);

  if (!pstate) {
    return;
  }

  if (stateCompleted(pstate->onSyncTransactions(*pcontext, rNum))) {
    handleTransitions(Event::Transactions);
  }

  // restore possibly cached hashes from other nodes
  // this is actual if conveyer has just stored last required block
  if (!recv_hash.empty() && cs::Conveyer::instance().currentRoundNumber() == rNum) {
    for (const auto& item : recv_hash) {
      if (stateCompleted(pstate->onHash(*pcontext, item.first, item.second))) {
        handleTransitions(Event::Hashes);
      }
    }
  }
}

const cs::PublicKey& SolverCore::getWriterPublicKey() const {
  // Previous solver returns confidant key with index equal result of takeDecision() method.
  // As analogue, found writer's index in stage3 if exists, otherwise return empty object as prev. solver does
  auto ptr = find_stage3(pnode->getConfidantNumber());
  if (ptr != nullptr) {
    const auto& trusted = cs::Conveyer::instance().confidants();
    if (trusted.size() >= ptr->writer) {
      return *(trusted.cbegin() + ptr->writer);
    }
  }
  // TODO: redesign getting ref to persistent object
  return SolverContext::zeroKey;
}

void SolverCore::gotHash(csdb::PoolHash&& hash, const cs::PublicKey& sender) {
  cs::Sequence delta = cs::Conveyer::instance().currentRoundNumber() - pnode->getBlockChain().getLastSequence();
  if (delta > 1) {
    recv_hash.push_back(std::make_pair<>(hash, sender));
    csdebug() << "SolverCore: cache hash until last block ready";
    return;
  }

  if (!pstate) {
    return;
  }

  if (stateCompleted(pstate->onHash(*pcontext, hash, sender))) {
    handleTransitions(Event::Hashes);
  }
}

void SolverCore::beforeNextRound() {
  if (!pstate) {
    return;
  }
  pstate->onRoundEnd(*pcontext, false /*is_bigbang*/);
}

void SolverCore::nextRound() {
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

  if (stateCompleted(pstate->onRoundTable(*pcontext, cs::Conveyer::instance().currentRoundNumber()))) {
    handleTransitions(Event::RoundTable);
  }
}

void SolverCore::gotStageOne(const cs::StageOne& stage) {
  if (find_stage1(stage.sender) != nullptr) {
    // duplicated
    return;
  }

  stageOneStorage.push_back(stage);
  csdebug() << "SolverCore: <-- stage-1 [" << static_cast<int>(stage.sender) << "] = " << stageOneStorage.size();

  if (!pstate) {
    return;
  }
  if (stateCompleted(pstate->onStage1(*pcontext, stage))) {
    handleTransitions(Event::Stage1Enough);
  }
}

void SolverCore::gotStageOneRequest(uint8_t requester, uint8_t required) {
  csdebug() << "SolverCore: [" << static_cast<int>(requester) << "] asks for stage-1 of [" << static_cast<int>(required) << "]";

  const auto ptr = find_stage1(required);
  if (ptr != nullptr) {
    pnode->sendStageReply(ptr->sender, ptr->signature, MsgTypes::FirstStage, requester);
  }
}

void SolverCore::gotStageTwoRequest(uint8_t requester, uint8_t required) {
  csdebug() << "SolverCore: [" << static_cast<int>(requester) << "] asks for stage-2 of [" << static_cast<int>(required) << "]";

  const auto ptr = find_stage2(required);
  if (ptr != nullptr) {
    pnode->sendStageReply(ptr->sender, ptr->signature, MsgTypes::SecondStage, requester);
  }
}

void SolverCore::gotStageThreeRequest(uint8_t requester, uint8_t required) {
  csdebug() << "SolverCore: [" << static_cast<int>(requester) << "] asks for stage-3 of [" << static_cast<int>(required) << "]";

  const auto ptr = find_stage3(required);
  if (ptr != nullptr) {
    pnode->sendStageReply(ptr->sender, ptr->signature, MsgTypes::ThirdStage, requester);
  }
}

void SolverCore::gotStageTwo(const cs::StageTwo& stage) {
  if (find_stage2(stage.sender) != nullptr) {
    // duplicated
    return;
  }

  stageTwoStorage.push_back(stage);
  csdebug() << "SolverCore: <-- stage-2 [" << static_cast<int>(stage.sender) << "] = " << stageTwoStorage.size();

  if (!pstate) {
    return;
  }

  if (stateCompleted(pstate->onStage2(*pcontext, stage))) {
    handleTransitions(Event::Stage2Enough);
  }
}

void SolverCore::printStage3(const cs::StageThree& stage) {
  std::string realTrustedString;

  for (auto& i : stage.realTrustedMask) {
    realTrustedString = realTrustedString + "[" + std::to_string(int(i)) + "] ";
  }

  csdebug() << "     SENDER = " << static_cast<int>(stage.sender) << ", WRITER = " << static_cast<int>(stage.writer) << ", RealTrusted = " << realTrustedString;
  csdebug() << "     BlockHash = " << cs::Utils::byteStreamToHex(stage.blockHash);
  csdebug() << "     BlockSign = " << cs::Utils::byteStreamToHex(stage.blockSignature);
  csdebug() << "     RoundHash = " << cs::Utils::byteStreamToHex(stage.roundHash);
  csdebug() << "     RoundSign = " << cs::Utils::byteStreamToHex(stage.roundSignature);
}

void SolverCore::gotStageThree(const cs::StageThree& stage, const uint8_t flagg) {
  if (find_stage3(stage.sender) != nullptr) {
    // duplicated
    return;
  }

  auto lamda = [this] (const cs::StageThree& stageFrom, const cs::StageThree& stageTo) {
    const cs::Conveyer& conveyer = cs::Conveyer::instance();
    if (!cscrypto::verifySignature(stageFrom.blockSignature, conveyer.confidantByIndex(stageFrom.sender),
                                   stageTo.blockHash.data(), stageTo.blockHash.size())) {
      cswarning() << "Block Signatures is not valid !";
      return;
    }

    if (!cscrypto::verifySignature(stageFrom.roundSignature, conveyer.confidantByIndex(stageFrom.sender),
                                   stageTo.roundHash.data(), stageTo.roundHash.size())) {
      cswarning() << "Round Signatures is not valid !";
      return;
    }

    if (!(stageFrom.realTrustedMask == stageTo.realTrustedMask)) {
      cswarning() << "Real Trusted is not valid !";
      return;
    }

    if (!(stageFrom.writer == stageTo.writer)) {
      cswarning() << "Writer is not valid !";
      return;
    }

    trueStageThreeStorage.emplace_back(stageFrom);
    pnode->addRoundSignature(stageFrom);
    csdebug() << "Stage3 [" << static_cast<int>(stageFrom.sender) << "] - signatures are OK";
  };

  switch (flagg) {
    case 0:
      break;

    case 1:
      // TODO: change the routine of pool signing
      for (const auto& st : stageThreeStorage) {
        lamda(st, stage);
      }
      trueStageThreeStorage.push_back(stage);
      pnode->addRoundSignature(stage);
      break;

    case 2:
      const auto st = find_stage3(pnode->getConfidantNumber());
      lamda(stage, *st);
      break;
  }

  stageThreeStorage.push_back(stage);

  csdebug() << "SolverCore: <-- stage-3 [" << static_cast<int>(stage.sender) << "] = " << stageThreeStorage.size()
            << " : " << trueStageThreeStorage.size();

  if (!pstate) {
    return;
  }

  if (stateCompleted(pstate->onStage3(*pcontext, stage))) {
    handleTransitions(Event::Stage3Enough);
  }

  if (stateFailed(pstate->onStage3(*pcontext, stage))) {
    pnode->getBlockChain().removeLastBlock();
    handleTransitions(Event::SetNormal);
  }
}

size_t SolverCore::trueStagesThree() {
  return trueStageThreeStorage.size();
}

size_t SolverCore::stagesThree() {
  return stageThreeStorage.size();
}

void SolverCore::send_wallet_transaction(const csdb::Transaction& tr) {
  // DEBUG:
#if defined(DEBUG_SMARTS)
  if (SmartContracts::is_smart_contract(tr)) {
    psmarts->force_execution = true;
  }
#endif
  if (psmarts->test_smart_contract_emits(tr)) {
    // avoid pass to conveyer until execution of emitter contract has finished
    csdebug() << "SolverCore: smart contract emits transaction";
    return;
  }

  cs::Conveyer::instance().addTransaction(tr);
}

void SolverCore::gotRoundInfoRequest(const cs::PublicKey& requester, cs::RoundNumber requester_round) {
  csdebug() << "SolverCore: got round info request from " << cs::Utils::byteStreamToHex(requester.data(), requester.size());
  auto& conveyer = cs::Conveyer::instance();

  if (requester_round == conveyer.currentRoundNumber()) {
    const auto ptr = /*cur_round == 10 ? nullptr :*/ find_stage3(pnode->getConfidantNumber());
    if (ptr != nullptr) {
      if (ptr->sender == ptr->writer) {
        if (pnode->tryResendRoundTable(requester, conveyer.currentRoundNumber())) {
          csdebug() << "SolverCore: re-send full round info #" << conveyer.currentRoundNumber() << " completed";
          return;
        }
      }
    }
    csdebug() << "SolverCore: also on the same round, inform cannot help with";
    pnode->sendRoundTableReply(requester, false);
  }
  else if (requester_round < conveyer.currentRoundNumber()) {
    if (conveyer.isConfidantExists(requester)) {
      if (pnode->tryResendRoundTable(requester, conveyer.currentRoundNumber())) {
        csdebug() << "SolverCore: requester is trusted next round, supply it with round info";
      }
      else {
        csdebug() << "SolverCore: try but cannot send full round info";
      }
      return;
    }
    csdebug() << "SolverCore: inform requester next round has come and it is not in trusted list";
    pnode->sendRoundTableReply(requester, true);
  }
  else {
    // requester_round > cur_round, cannot help with!
    csdebug() << "SolverCore: cannot help with outrunning round info";
  }
}

void SolverCore::gotRoundInfoReply(bool next_round_started, const cs::PublicKey& /*respondent*/) {
  if (next_round_started) {
    csdebug() << "SolverCore: round info reply means next round started, and I am not trusted node. Waiting next round";
    return;
  }
  csdebug() << "SolverCore: round info reply means next round is not started, become writer";
  handleTransitions(SolverCore::Event::SetWriter);
}
}  // namespace cs
