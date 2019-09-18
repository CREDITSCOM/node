#include <consensus.hpp>
#include <smartcontracts.hpp>
#include <solvercontext.hpp>
#include <solvercore.hpp>

#include <csnode/conveyer.hpp>
#include <csnode/fee.hpp>
#include <csnode/node.hpp>

#include <csdb/currency.hpp>
#include <lib/system/logger.hpp>

#include <chrono>
#include <algorithm>

namespace cs {
void SolverCore::init(const cs::PublicKey& pub, const cs::PrivateKey& priv) {
    public_key = pub;
    private_key = priv;

    auto pconnector = pnode->getConnector();

    if (pconnector != nullptr) {
        psmarts->init(pub, pnode);
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
            if (stateCompleted(pstate->onHash(*pcontext, item.hash, item.sender))) {
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
    return Zero::key;
}

bool SolverCore::checkNodeCache(const cs::PublicKey& sender) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::StartingDPOS) {
        csdebug() << "The DPOS doesn't work unless the roundNumber is less than " << Consensus::StartingDPOS;
        return true;
    }
    BlockChain::WalletData wData;
    pnode->getBlockChain().findWalletData(csdb::Address::from_public_key(sender), wData);
    if (wData.balance_ < Consensus::MinStakeValue) {
        return false;
    }
    return true;
}

void SolverCore::addToGraylist(const cs::PublicKey & sender, uint32_t rounds) {

    if (grayList_.find(sender) == grayList_.cend()) {
        grayList_.emplace(sender, rounds);
        csdebug() << "Node " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " is in gray list now";
    }
    else {
        grayList_[sender] += uint16_t(rounds * 2);
        csdebug() << "Node " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " will continue its being in gray list now";
    }
}

void SolverCore::gotHash(const cs::StageHash&& sHash, uint8_t currentTrustedSize) {
    // GrayList check
    if (grayList_.count(sHash.sender) > 0) {
        csdebug() << "The sender " << cs::Utils::byteStreamToHex(sHash.sender.data(), sHash.sender.size()) << " is in gray list";
        return;
    }

    // DPOS check start -> comment if unnecessary
    if (!checkNodeCache(sHash.sender)) {
        csdebug() << "The sender's cash value is too low -> Don't allowed to be a confidant";
        return;
    }
    // DPOS check finish

    if (sHash.realTrustedSize < currentTrustedSize) {
        csdebug() << "Stake value is lower than that in this node, trow this hash";
    }
    auto rNum = cs::Conveyer::instance().currentRoundNumber();
    auto it = std::find_if(recv_hash.cbegin(), recv_hash.cend(), [sHash, rNum](const cs::StageHash& sh)
    { return ((sHash.sender == sh.sender) && (sHash.realTrustedSize > sh.realTrustedSize) && (sHash.round == rNum)); });
    if (it != recv_hash.cend()) {
        recv_hash.erase(it);
    }
    recv_hash.push_back(sHash);

    cs::Sequence delta = cs::Conveyer::instance().currentRoundNumber() - pnode->getBlockChain().getLastSeq();
    if (delta > 1) {
        //recv_hash.push_back(std::make_pair<>(sHash.hash, sHash.sender));
        csdebug() << "SolverCore: cache hash until last block ready";
        return;
    }

    if (!pstate) {
        return;
    }

    if (stateCompleted(pstate->onHash(*pcontext, sHash.hash, sHash.sender))) {
        handleTransitions(Event::Hashes);
    }
}

void SolverCore::beforeNextRound() {
    if (!pstate) {
        return;
    }
    pstate->onRoundEnd(*pcontext, false /*is_bigbang*/);
}

void SolverCore::nextRound(bool updateRound) {
    // as store result of current round:
    if (Consensus::Log) {
        csdebug() << "SolverCore: clear all stored round data (block hashes, stages-1..3)";
    }
    if (!updateRound) {
        recv_hash.clear();
    }

    stageOneStorage.clear();
    stageTwoStorage.clear();
    stageThreeStorage.clear();
    trueStageThreeStorage.clear();
    trusted_candidates.clear();
    realTrustedChanged_ = false;
    tempRealTrusted_.clear();
    currentStage3iteration_ = 0;
    updateGrayList(cs::Conveyer::instance().currentRoundNumber());
    lastSentSignatures_.poolSignatures.clear();
    lastSentSignatures_.roundSignatures.clear();
    lastSentSignatures_.trustedConfirmation.clear();

    if (!pstate) {
        return;
    }

    if (stateCompleted(pstate->onRoundTable(*pcontext, cs::Conveyer::instance().currentRoundNumber()))) {
        handleTransitions(Event::RoundTable);
    }
}

void SolverCore::gotStageOne(const cs::StageOne& stage) {
    if (find_stage1(stage.sender) != nullptr) {
        uint64_t lastTimeStamp, currentTimeStamp;
        uint8_t sender = stage.sender;
        try {
            lastTimeStamp = std::stoll(find_stage1(stage.sender)->roundTimeStamp);
        }
        catch (...) {
            csdebug() << __func__ << ": last stage-1 from " << static_cast<int>(stage.sender) << " Timestamp was announced as zero";
            auto it = std::find_if(stageOneStorage.begin(), stageOneStorage.end(), [sender](cs::StageOne& st) { return st.sender == sender;});
            stageOneStorage.erase(it);
            //erase this stage
        }

        try {
            currentTimeStamp = std::stoll(stage.roundTimeStamp);
        }
        catch (...) {
            csdebug() << __func__ << ": current stage-1 from " << static_cast<int>(stage.sender) << " Timestamp was announced as zero";
            return;
        }
        // duplicated
        if (currentTimeStamp > lastTimeStamp) {
            auto it = std::find_if(stageOneStorage.begin(), stageOneStorage.end(), [sender](cs::StageOne& st) { return st.sender == sender; });
            stageOneStorage.erase(it);
        }
        else {
            return;
        }
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
        pnode->sendStageReply(ptr->sender, ptr->signature, MsgTypes::FirstStage, requester, ptr->messageBytes);
    }
}

void SolverCore::gotStageTwoRequest(uint8_t requester, uint8_t required) {
    csdebug() << "SolverCore: [" << static_cast<int>(requester) << "] asks for stage-2 of [" << static_cast<int>(required) << "]";

    const auto ptr = find_stage2(required);
    if (ptr != nullptr) {
        pnode->sendStageReply(ptr->sender, ptr->signature, MsgTypes::SecondStage, requester, ptr->messageBytes);
    }
}

uint8_t SolverCore::currentStage3iteration() {
    return currentStage3iteration_;
}

void SolverCore::gotStageThreeRequest(uint8_t requester, uint8_t required /*, uint8_t iteration*/) {
    csdebug() << "SolverCore: [" << static_cast<int>(requester) << "] asks for stage-3 of [" << static_cast<int>(required) << "]";  // - i" << static_cast<int>(iteration);

    // const auto ptr = find_stage3(required);

    for (auto& it : stageThreeStorage) {
        if (it.iteration == currentStage3iteration_ && it.sender == requester) {
            pnode->sendStageReply(it.sender, it.signature, MsgTypes::ThirdStage, requester, it.messageBytes);
            return;
        }
    }
    csdebug() << "SolverCore: don't have the requested stage three";
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

void SolverCore::gotStageThree(const cs::StageThree& stage, const uint8_t flagg) {
    if (stage.iteration < currentStage3iteration_) {
        // stage with old iteration
        return;
    }
    auto ptr = find_stage3(stage.sender, stage.iteration);
    if (ptr != nullptr) {
        return;
    }

    auto lamda = [this](const cs::StageThree& stageFrom, const cs::StageThree& stageTo) {
        const cs::Conveyer& conveyer = cs::Conveyer::instance();
        bool markedUntrusted = false;
        if (stageTo.realTrustedMask[stageFrom.sender] == cs::ConfidantConsts::InvalidConfidantIndex) {
            markedUntrusted = true;
        }
		bool invalidBlockSignatures = false;
        if (!cscrypto::verifySignature(stageFrom.blockSignature, conveyer.confidantByIndex(stageFrom.sender), stageTo.blockHash.data(), stageTo.blockHash.size())) {
			invalidBlockSignatures = true;
        }
		bool invalidRoundSignatures = false;
        if (!cscrypto::verifySignature(stageFrom.roundSignature, conveyer.confidantByIndex(stageFrom.sender), stageTo.roundHash.data(), stageTo.roundHash.size())) {
			invalidRoundSignatures = true;
        }
		bool invalidTrustedSignatures = false;
        if (!cscrypto::verifySignature(stageFrom.trustedSignature, conveyer.confidantByIndex(stageFrom.sender), stageTo.trustedHash.data(), stageTo.trustedHash.size())) {
			invalidTrustedSignatures = true;
        }
		bool invalidRealTrustedMask = false;
        if (!(stageFrom.realTrustedMask == stageTo.realTrustedMask) || stageTo.realTrustedMask[stageFrom.sender] == cs::ConfidantConsts::InvalidConfidantIndex) {
			invalidRealTrustedMask = true;
        }
		bool invalidWriter = false;
        if (!(stageFrom.writer == stageTo.writer)) {
			invalidWriter = true;
        }

        if (markedUntrusted || invalidBlockSignatures || invalidRoundSignatures || invalidTrustedSignatures || invalidRealTrustedMask || invalidWriter){
			cswarning() << "Stage3 from T[" << static_cast<int>(stageFrom.sender) << "] - final check ... NOT PASSED! This problem will be resolved automatically.";
			csdebug() << "The stage below has next problems:";
			if (markedUntrusted) {
				csdebug() << "--> The node, that sent this stage was marked as untrusted";
			}
            if (stageTo.realTrustedMask[stageFrom.sender] != cs::ConfidantConsts::InvalidConfidantIndex) {
				if (invalidBlockSignatures) {
					csdebug() << "--> Block Signatures are not valid";
				}
				if (invalidRoundSignatures) {
					csdebug() << "--> Round Signatures are not valid.";
				}
				if (invalidTrustedSignatures) {
					csdebug() << "--> Trusted Signatures are not valid.";
				}
				if (invalidRealTrustedMask) {
					csdebug() << "--> Real Trusted are not valid.";
				}
				if (invalidWriter) {
					csdebug() << "--> Writer is not valid.";
				}
                csdebug() << cs::StageThree::toString(stageFrom);
                realTrustedSetValue(stageFrom.sender, cs::ConfidantConsts::InvalidConfidantIndex);
            }
            return;
        }

        // if (getRealTrusted()[stageFrom.sender] == cs::ConfidantConsts::InvalidConfidantIndex) {
        //  realTrustedSet(stageFrom.sender, cs::ConfidantConsts::FirstWriterIndex);
        //}
        trueStageThreeStorage.emplace_back(stageFrom);
        addRoundSignature(stageFrom);
        csdebug() << "Stage3 from T[" << static_cast<int>(stageFrom.sender) << "] - final check ... passed!";
    };

    switch (flagg) {
        case 0:
            break;

        case 1:
            // TODO: change the routine of pool signing
            for (const auto& st : stageThreeStorage) {
                if (st.iteration == currentStage3iteration_) {
                    lamda(st, stage);
                }
            }
            trueStageThreeStorage.push_back(stage);
            addRoundSignature(stage);
            break;

        case 2:
            const auto st = find_stage3(pnode->getConfidantNumber());
            if (st != nullptr && stage.iteration == st->iteration) {
                lamda(stage, *st);
            }
            break;
    }

    stageThreeStorage.push_back(stage);

    csdebug() << "SolverCore: <-- stage-3 [" << static_cast<int>(stage.sender) << "] = " << stageThreeStorage.size() << " : " << trueStageThreeStorage.size();

    if (!pstate) {
        return;
    }

    switch (pstate->onStage3(*pcontext, stage)) {
        case Result::Finish:
            handleTransitions(Event::Stage3Enough);
            break;
        case Result::Retry:
            ++currentStage3iteration_;
            adjustStageThreeStorage();
            handleTransitions(Event::Stage3NonComplete);
            break;
        case Result::Failure:
            cserror() << "SolverCore: error in state " << (pstate ? pstate->name() : "null  - Consensus state can't be completed. Trying to resolve ... ");
            removeDeferredBlock(deferredBlock_.sequence());
            handleTransitions(Event::SetNormal);
            break;
        default:
            break;
    }
}

void SolverCore::addRoundSignature(const cs::StageThree& st3) {
    size_t pos = 0;
    for (size_t i = 0; i < st3.realTrustedMask.size(); i++) {
        if (i == static_cast<size_t>(st3.sender)) {
            break;
        }
        if (st3.realTrustedMask[i] != cs::ConfidantConsts::InvalidConfidantIndex) {
            ++pos;
        }
    }
    csdebug() << "NODE> pos = " << pos
        << ", poolSigsSize = " << lastSentSignatures_.poolSignatures.size()
        << ", rtSigsSize = " << lastSentSignatures_.roundSignatures.size()
        << ", roundSigsSize = " << lastSentSignatures_.trustedConfirmation.size();
    //  if (lastSentSignatures_.poolSignatures.size() == 0) {
    size_t tCount = static_cast<int>(cs::TrustedMask::trustedSize(st3.realTrustedMask));
    lastSentSignatures_.poolSignatures.resize(tCount);
    lastSentSignatures_.roundSignatures.resize(tCount);
    lastSentSignatures_.trustedConfirmation.resize(tCount);
    //  }
    std::copy(st3.blockSignature.cbegin(), st3.blockSignature.cend(), lastSentSignatures_.poolSignatures[pos].begin());
    std::copy(st3.roundSignature.cbegin(), st3.roundSignature.cend(), lastSentSignatures_.roundSignatures[pos].begin());
    std::copy(st3.trustedSignature.cbegin(), st3.trustedSignature.cend(), lastSentSignatures_.trustedConfirmation[pos].begin());

    csdebug() << "NODE> Adding signatures of stage3 from T(" << cs::numeric_cast<int>(st3.sender)
        << ") = " << lastSentSignatures_.roundSignatures.size();

}

void SolverCore::adjustStageThreeStorage() {
    std::vector<cs::StageThree> tmpStageThreeStorage;
    for (auto& it : stageThreeStorage) {
        if (it.iteration == currentStage3iteration_) {
            tmpStageThreeStorage.push_back(it);
        }
    }
    stageThreeStorage.clear();
    stageThreeStorage = tmpStageThreeStorage;
    trueStageThreeStorage.clear();  // how to put the realTrusted value to the on-stage3
    pnode->adjustStageThreeStorage();
}

size_t SolverCore::trueStagesThree() {
    return trueStageThreeStorage.size();
}

bool SolverCore::realTrustedChanged() const {
    return realTrustedChanged_;
}

void SolverCore::realTrustedChangedSet(bool val) {
    realTrustedChanged_ = val;
}

void SolverCore::realTrustedSetValue(cs::Byte position, cs::Byte value) {
    csdebug() << __func__ << ": realtrusted in solvercore set, realTrustedChanged switched to true";
    realTrustedChangedSet(true);
    size_t pos = static_cast<size_t>(position);
    if (tempRealTrusted_.size() > pos) {
        tempRealTrusted_[pos] = value;
    }
}

void SolverCore::realTrustedSet(cs::Bytes realTrusted) {
    tempRealTrusted_ = realTrusted;
}

void SolverCore::updateGrayList(cs::RoundNumber round) {
    csdebug() << __func__;
    if (lastGrayUpdated_ >= round) {
        csdebug() << "Gray list will update only if the round number changes";
        return;
    }
    const uint16_t delta = uint16_t(round - lastGrayUpdated_);
    lastGrayUpdated_ = round;

    auto it = grayList_.begin();
    while (it != grayList_.end()) {
        if (it->second <= delta) {
            csdebug() << "Node with PK " << cs::Utils::byteStreamToHex(it->first.data(), it->first.size()) << " freed from grayList trap";
            it = grayList_.erase(it);
        }
        else {
            it->second -= delta;
            ++it;
        }
    }

}

cs::Bytes SolverCore::getRealTrusted() {
    return tempRealTrusted_;
}

size_t SolverCore::stagesThree() {
    return stageThreeStorage.size();
}

void SolverCore::send_wallet_transaction(const csdb::Transaction& tr) {
    if (psmarts->capture_transaction(tr)) {
        // avoid pass to conveyer, psmarts provide special handling
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

bool SolverCore::isContractLocked(const csdb::Address& address) const {
    return psmarts->is_contract_locked(address);
}

}  // namespace cs
