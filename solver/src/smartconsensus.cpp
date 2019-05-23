#include <smartconsensus.hpp>
#include <smartcontracts.hpp>

#pragma warning(push)
#pragma warning(disable : 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)

#include <csdb/amount.hpp>
#include <csdb/currency.hpp>
#include <csnode/datastream.hpp>
#include <solvercore.hpp>

namespace {
const char* kLogPrefix = "Smart: ";
}

namespace cs {

SmartConsensus::SmartConsensus() {
    pnode_ = nullptr;
    psmarts_ = nullptr;
}

SmartConsensus::~SmartConsensus() {
    cslog() << kLogPrefix << "======================  SMART-ROUND {" << smartRoundNumber_ << '.' << smartTransaction_ << "} END =====================";
    killTimer();
    pnode_->removeSmartConsensus(id());
    cs::Connector::disconnect(&pnode_->gotSmartStageOne, this, &cs::SmartConsensus::addSmartStageOne);
    cs::Connector::disconnect(&pnode_->gotSmartStageTwo, this, &cs::SmartConsensus::addSmartStageTwo);
    cs::Connector::disconnect(&pnode_->gotSmartStageThree, this, &cs::SmartConsensus::addSmartStageThree);
    cs::Connector::disconnect(&pnode_->receivedSmartStageRequest, this, &cs::SmartConsensus::gotSmartStageRequest);
}

const std::vector<cs::PublicKey>& SmartConsensus::smartConfidants() const {
    return smartConfidants_;
}

bool SmartConsensus::initSmartRound(const cs::TransactionsPacket& pack, uint8_t runCounter, Node* node, SmartContracts* smarts) {
    trustedChanged_ = false;
    smartStageThreeSent_ = false;
    pnode_ = node;
    psmarts_ = smarts;
    smartConfidants_.clear();
    runCounter_ = runCounter;
    smartRoundNumber_ = 0;
    smartTransaction_ = std::numeric_limits<uint32_t>::max();
    timeoutStageCounter_ = 0;
    // csdb::Address abs_addr;
    csdb::Amount executor_fee{0};
    cs::TransactionsPacket tmpPacket;
    bool primary_new_state_found = false;

    for (const auto& tr : pack.transactions()) {
        // only the 1st new_state is specifically handled
        if (SmartContracts::is_new_state(tr) && !primary_new_state_found) {
            primary_new_state_found = true;
            // abs_addr = smarts->absolute_address(tr.source());

            csdb::UserField fld = tr.user_field(trx_uf::new_state::RefStart);
            if (fld.is_valid()) {
                SmartContractRef ref(fld);
                if (ref.is_valid()) {
                    smartRoundNumber_ = ref.sequence;
                    smartTransaction_ = static_cast<decltype(smartTransaction_)>(ref.transaction);
                }
            }
            csdb::UserField fld2 = tr.user_field(trx_uf::new_state::Fee);
            if (fld2.is_valid()) {
                executor_fee += fld2.value<csdb::Amount>();
            }
            // break;
            // creating fee free copy of state transaction
            tmpNewState_.set_amount(tr.amount());
            tmpNewState_.set_source(tr.source());
            tmpNewState_.set_target(tr.target());
            tmpNewState_.set_currency(tr.currency());
            tmpNewState_.set_counted_fee(tr.counted_fee());
            tmpNewState_.set_currency(tr.currency());
            tmpNewState_.set_innerID(tr.innerID());

            tmpNewState_.add_user_field(trx_uf::new_state::Count, tr.user_field(trx_uf::new_state::Count));
            tmpNewState_.add_user_field(trx_uf::new_state::RefStart, tr.user_field(trx_uf::new_state::RefStart));
            tmpNewState_.add_user_field(trx_uf::new_state::RetVal, tr.user_field(trx_uf::new_state::RetVal));
            tmpNewState_.add_user_field(trx_uf::new_state::Value, tr.user_field(trx_uf::new_state::Value));

            tmpPacket.addTransaction(tmpNewState_);
        }
        else {
            tmpPacket.addTransaction(tr);
        }
    }

    if (!primary_new_state_found || 0 == smartRoundNumber_ || std::numeric_limits<uint32_t>::max() == smartTransaction_) {
        cserror() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} smart contract result packet must contain new state transaction";
        return false;
    }

    csdebug() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} consensus starting on R-" << cs::Conveyer::instance().currentRoundNumber() << "... ";

    smartConfidants_ = pnode_->retriveSmartConfidants(smartRoundNumber_);
    ownSmartsConfNum_ = calculateSmartsConfNum();
    refreshSmartStagesStorage();
    if (ownSmartsConfNum_ == cs::InvalidConfidantIndex) {
        cserror() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} cannot determine own number in confidant list";
        return false;
    }

    cslog() << "======================  SMART-ROUND: {" << smartRoundNumber_ << '.' << smartTransaction_ << "} [" << static_cast<int>(ownSmartsConfNum_)
            << "] =========================";
    csdebug() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} SMART confidants (" << smartConfidants_.size()
              << "), proposed fee: " << executor_fee.to_string(18);

    // pack_.transactions(0).user_field(1) = 0;

    currentSmartTransactionPack_ = pack;

    tmpPacket.makeHash();
    auto tmp = tmpPacket.hash().toBinary();
    std::copy(tmp.cbegin(), tmp.cend(), st1.hash.begin());
    st1.fee = executor_fee;
    // signals subscription
    cs::Connector::connect(&pnode_->gotSmartStageOne, this, &cs::SmartConsensus::addSmartStageOne);
    cs::Connector::connect(&pnode_->gotSmartStageTwo, this, &cs::SmartConsensus::addSmartStageTwo);
    cs::Connector::connect(&pnode_->gotSmartStageThree, this, &cs::SmartConsensus::addSmartStageThree);
    cs::Connector::connect(&pnode_->receivedSmartStageRequest, this, &cs::SmartConsensus::gotSmartStageRequest);
    st1.id = id();
    pnode_->addSmartConsensus(st1.id);
    st1.sender = ownSmartsConfNum_;
    addSmartStageOne(st1, true);
    return true;
}

uint8_t SmartConsensus::calculateSmartsConfNum() {
    uint8_t i = 0;
    uint8_t ownSmartConfNumber = cs::InvalidConfidantIndex;
    for (auto& e : smartConfidants_) {
        if (e == pnode_->getNodeIdKey()) {
            ownSmartConfNumber = i;
        }
        csdebug() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "} [" << static_cast<int>(i) << "] "
                  << (ownSmartConfNumber != cs::InvalidConfidantIndex && i == ownSmartConfNumber ? "me" : cs::Utils::byteStreamToHex(e.data(), e.size()));
        ++i;
    }

    if (ownSmartConfNumber == cs::InvalidConfidantIndex) {
        csdebug() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "} This NODE is not a confidant one for this smart-contract consensus round";
    }

    return ownSmartConfNumber;
}

uint8_t SmartConsensus::ownSmartsConfidantNumber() {
    return ownSmartsConfNum_;
}

cs::Sequence SmartConsensus::smartRoundNumber() {
    return smartRoundNumber_;
}

void SmartConsensus::refreshSmartStagesStorage() {
    csdetails() << "          " << __func__;
    size_t cSize = smartConfidants_.size();
    smartStageOneStorage_.clear();
    smartStageOneStorage_.resize(cSize);
    smartStageTwoStorage_.clear();
    smartStageTwoStorage_.resize(cSize);
    smartStageThreeStorage_.clear();
    smartStageThreeStorage_.resize(cSize);

    for (size_t i = 0; i < cSize; ++i) {
        smartStageOneStorage_.at(i).sender = cs::ConfidantConsts::InvalidConfidantIndex;
        smartStageTwoStorage_.at(i).sender = cs::ConfidantConsts::InvalidConfidantIndex;
        smartStageThreeStorage_.at(i).sender = cs::ConfidantConsts::InvalidConfidantIndex;
    }

    memset(&st1, 0, sizeof(st1));

    st2.signatures.clear();
    st2.signatures.resize(cSize);
    st2.hashes.clear();
    st2.hashes.resize(cSize);
    st2.id = 0;
    st3.realTrustedMask.clear();
    st3.realTrustedMask.resize(cSize);
    st3.packageSignature.fill(0);
    st2.sender = cs::ConfidantConsts::InvalidConfidantIndex;
    st3.sender = cs::ConfidantConsts::InvalidConfidantIndex;
    st3.writer = cs::ConfidantConsts::InvalidConfidantIndex;
    st3.id = 0;

    memset(st3.signature.data(), 0, st3.signature.size());
    memset(st2.signature.data(), 0, st3.signature.size());

    // smartStagesStorageClear(cSize);

    smartUntrusted.clear();
    smartUntrusted.resize(cSize);
    smartConsensusMask.clear();
    smartConsensusMask.resize(cSize);
    std::fill(smartConsensusMask.begin(), smartConsensusMask.end(), cs::ConfidantConsts::InvalidConfidantIndex);
    std::fill(smartUntrusted.begin(), smartUntrusted.end(), 0);
    startTimer(1);
}

void SmartConsensus::addSmartStageOne(cs::StageOneSmarts& stage, bool send) {
    if (stage.id != id()) {
        return;
    }
    csmeta(csdetails) << "start";
    if (send) {
        pnode_->sendSmartStageOne(smartConfidants_, stage);
    }
    if (smartStageOneStorage_.size() <= (size_t)stage.sender) {
        // normally unexpected
        return;
    }
    if (smartStageOneStorage_.at(stage.sender).sender == stage.sender) {
        return;
    }
    if (!std::equal(stage.hash.cbegin(), stage.hash.cend(), Zero::hash.cbegin())) {
        smartConsensusMask[stage.sender] = 0;
    }
    else {
        smartConsensusMask[stage.sender] = 254;
    }
    smartStageOneStorage_.at(stage.sender) = stage;
    std::string stagesPlot;
    for (size_t i = 0; i < smartConfidants_.size(); ++i) {
        // csdebug() << log_prefix << "[" << i << "] - " << static_cast<int>(smartStageOneStorage_.at(i).sender);
        stagesPlot = stagesPlot + '[' + std::to_string(static_cast<int>(smartStageOneStorage_.at(i).sender)) + "] ";
    }
    csdebug() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "}  <-- SMART-Stage-1 " << stagesPlot;
    st2.signatures.at(stage.sender) = stage.signature;
    st2.hashes.at(stage.sender) = stage.messageHash;
    if (smartStageOneEnough()) {
        killTimer();
        cs::Connector::disconnect(&pnode_->gotSmartStageOne, this, &cs::SmartConsensus::addSmartStageOne);
        st2.sender = ownSmartsConfNum_;
        st2.id = id();
        addSmartStageTwo(st2, true);
        uint8_t index = 0;
        for (auto it : smartConsensusMask) {
            if (it == 255 || it == 254) {
                fake_stage2(index);
            }
        }
        startTimer(2);
    }
}

void SmartConsensus::addSmartStageTwo(cs::StageTwoSmarts& stage, bool send) {
    if (stage.id != id()) {
        return;
    }
    if (send) {
        pnode_->sendSmartStageTwo(smartConfidants_, stage);
    }
    if (smartStageTwoStorage_.size() <= (size_t)stage.sender) {
        // normally unexpected
        return;
    }
    auto& stageTwo = smartStageTwoStorage_.at(stage.sender);
    if (stageTwo.sender == stage.sender) {
        return;
    }
    // stageTwo = stage;
    std::string stagesPlot;
    for (size_t i = 0; i < smartConfidants_.size(); ++i) {
        smartStageTwoStorage_.at(stage.sender) = stage;
        stagesPlot = stagesPlot + '[' + std::to_string(static_cast<int>(smartStageTwoStorage_.at(i).sender)) + "] ";
    }
    csdebug() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "}  <-- SMART-Stage-2 - SmartRound {" << blockPart(stage.id) << '.'
              << transactionPart(stage.id) << "} " << stagesPlot;
    if (smartStageTwoEnough()) {
        killTimer();
        cs::Connector::disconnect(&pnode_->gotSmartStageTwo, this, &cs::SmartConsensus::addSmartStageTwo);
        processStages();
    }
}

// cs::PublicKey SmartConsensus::smartAddress() {
//  return smartAddress_;
//}

void SmartConsensus::processStages() {
    csmeta(csdetails) << "start";
    const size_t cnt = smartConfidants_.size();
    // perform the evaluation og stages 1 & 2 to find out who is traitor
    int hashFrequency = 1;
    const auto& hash_t = smartStageOneStorage_.at(ownSmartsConfNum_).hash;
    for (auto& st : smartStageOneStorage_) {
        if (st.sender == ownSmartsConfNum_) {
            continue;
        }
        if (st.hash != hash_t) {
            ++(smartUntrusted.at(st.sender));
            cslog() << kLogPrefix << "Confidant [" << static_cast<int>(st.sender) << "] is marked as untrusted (wrong hash)";
        }
        else {
            ++hashFrequency;
        }
    }
    csdebug() << kLogPrefix << "{" << smartRoundNumber_ << "} Hash " << cs::Utils::byteStreamToHex(hash_t.data(), hash_t.size()) << ", Frequency = " << hashFrequency;
    auto& myStage2 = smartStageTwoStorage_.at(ownSmartsConfNum_);
    for (auto& st : smartStageTwoStorage_) {
        if (st.sender == ownSmartsConfNum_) {
            continue;
        }
        for (size_t i = 0; i < cnt; ++i) {
            if (st.signatures[i] != myStage2.signatures[i]) {
                if (cscrypto::verifySignature(st.signatures[i], smartConfidants_[i], st.hashes[i].data(), sizeof(st.hashes[i]))) {
                    ++(smartUntrusted.at(i));
                    cslog() << kLogPrefix << "Confidant [" << i << "] is marked as untrusted (wrong hash)";
                }
                else {
                    ++(smartUntrusted.at(st.sender));
                    cslog() << kLogPrefix << "Confidant [" << static_cast<int>(st.sender) << "] is marked as untrusted (wrong signature)";
                }
            }
        }
    }
    size_t cnt_active = 0;
    cs::StageThreeSmarts stage;
    stage.realTrustedMask.resize(cnt);
    for (size_t i = 0; i < cnt; ++i) {
        stage.realTrustedMask[i] = (smartUntrusted[i] > 0 ? cs::ConfidantConsts::InvalidConfidantIndex : cs::ConfidantConsts::FirstWriterIndex);
        if (stage.realTrustedMask[i] == cs::ConfidantConsts::FirstWriterIndex) {
            ++cnt_active;
        }
    }
    const size_t lowerTrustedLimit = static_cast<size_t>(smartConfidants_.size() / 2. + 1.);
    if (cnt_active < lowerTrustedLimit) {
        cslog() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} smart consensus is NOT achieved, the state transaction won't send to the conveyer";
        return;
    }
    csdebug() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} smart consensus achieved";

    if (hash_t.empty()) {
        return;  // TODO: decide what to return
    }
    int k = *(unsigned int*)hash_t.data();
    if (k < 0) {
        k = -k;
    }
    csdebug() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} smart consensus result 1 from 3";
    size_t idx_writer = static_cast<size_t>(k % cnt_active);
    size_t idx = 0;

    csdb::Amount sumFee(0);
    // here will the fee be calculated too
    for (size_t i = 0; i < cnt; ++i) {
        if (st3.realTrustedMask.at(i) != InvalidConfidantIndex) {
            sumFee += smartStageOneStorage_.at(i).fee;
            if (idx == idx_writer) {
                st3.writer = static_cast<uint8_t>(i);
            }
            ++idx;
        }
    }
    csdb::Amount finalFee = calculateFinalFee(sumFee, idx);
    csdebug() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} smart consensus result 2 from 3";
    idx = 0;
    for (size_t i = st3.writer; i < cnt + st3.writer; ++i) {
        size_t c = i % cnt;
        if (st3.realTrustedMask.at(c) != InvalidConfidantIndex) {
            st3.realTrustedMask.at(c) = static_cast<uint8_t>(idx);
            ++idx;
        }
    }
    csdebug() << kLogPrefix << "{" << smartRoundNumber_ << "} smart consensus result 3 from 3";
    startTimer(3);
    createFinalTransactionSet(finalFee);
    st3.packageSignature =
        cscrypto::generateSignature(pnode_->getSolver()->getPrivateKey(), finalSmartTransactionPack_.hash().toBinary().data(), finalSmartTransactionPack_.hash().toBinary().size());
    csmeta(cslog) << "done";
    st3.id = id();
    st3.sender = ownSmartsConfNum_;
    st3.iteration = 0U;
    addSmartStageThree(st3, true);
}

// TODO: finalize the function
csdb::Amount SmartConsensus::calculateFinalFee(const csdb::Amount& finalFee, size_t realTrustedAmount) {
    csdb::Amount fee{0};
    uint32_t trustedNumber = static_cast<uint32_t>(realTrustedAmount);
    fee += finalFee;
    fee /= (trustedNumber * trustedNumber);
    // csdebug() << "Fee 4 = " << fee.to_string(17);
    // fee /= 1000;
    // csdebug() << "Fee 5 = " << fee.to_string(17);
    fee = fee * static_cast<int32_t>(realTrustedAmount);  // the overloaded operator *= doesn't work correct
    csdebug() << "Final fee = " << fee.to_string(17);
    return fee;
}

void SmartConsensus::addSmartStageThree(cs::StageThreeSmarts& stage, bool send) {
    if (stage.id != id()) {
        return;
    }

    auto lambda = [this](const cs::StageThreeSmarts& stageFrom, cs::Bytes hash) {
        if (!cscrypto::verifySignature(stageFrom.packageSignature, smartConfidants().at(stageFrom.sender), hash.data(), hash.size())) {
            cslog() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} ____ The signature is not valid";
            return;  // returns this function if the signature of smartco
        }
        smartStageThreeStorage_.at(stageFrom.sender) = stageFrom;
    };

    if (send) {
        csdebug() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} ____ 1.";
        pnode_->sendSmartStageThree(smartConfidants_, stage);
        smartStageThreeSent_ = true;
    }
    if (smartStageThreeStorage_.size() <= (size_t)stage.sender) {
        // normally unexpected
        return;
    }
    if (smartStageThreeStorage_.at(stage.sender).sender == stage.sender) {
        // avoid duplication
        return;
    }

    if (stage.sender != ownSmartsConfNum_) {
        if (smartStageThreeSent_ == false) {
            smartStageThreeTempStorage_.push_back(stage);
        }
        else {
            // const auto& hash = smartStageOneStorage_.at(stage.sender).hash;
            lambda(stage, finalSmartTransactionPack_.hash().toBinary());
        }
    }
    else {
        smartStageThreeStorage_.at(stage.sender) = stage;
        for (auto& it : smartStageThreeTempStorage_) {
            lambda(it, finalSmartTransactionPack_.hash().toBinary());
            csdebug() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "} <-- SMART-Stage-3 [" << static_cast<int>(stage.sender)
                      << "] = " << smartStage3StorageSize();
        }
    }

    csdebug() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "} <-- SMART-Stage-3 [" << static_cast<int>(stage.sender)
              << "] = " << smartStage3StorageSize();
    if (smartStageThreeSent_ && smartStageThreeEnough()) {
        killTimer();
        cs::Connector::disconnect(&pnode_->gotSmartStageThree, this, &cs::SmartConsensus::addSmartStageThree);
        if (finalSmartTransactionPack_.isHashEmpty()) {
            cserror() << kLogPrefix << "Trying to send FinalTransactionSet that doesn't exest";
            return;
        }
        sendFinalTransactionSet();
    }
}

size_t SmartConsensus::smartStage3StorageSize() {
    return std::count_if(smartStageThreeStorage_.begin(), smartStageThreeStorage_.end(),
                         [](const cs::StageThreeSmarts& it) { return it.sender != cs::ConfidantConsts::InvalidConfidantIndex; });
}

void SmartConsensus::createFinalTransactionSet(const csdb::Amount finalFee) {
    bool primary_new_state_found = false;
    for (const auto& tr : currentSmartTransactionPack_.transactions()) {
        if (!primary_new_state_found && SmartContracts::is_new_state(tr)) {
            primary_new_state_found = true;
            tmpNewState_.add_user_field(trx_uf::new_state::Fee, finalFee);
            finalSmartTransactionPack_.addTransaction(tmpNewState_);
        }
        else {
            finalSmartTransactionPack_.addTransaction(tr);
        }
    }
    finalSmartTransactionPack_.makeHash();
}

void SmartConsensus::sendFinalTransactionSet() {
    csmeta(csdetails) << "<starting> ownSmartConfNum = " << static_cast<int>(ownSmartsConfNum_)
                      << ", writer = " << static_cast<int>(smartStageThreeStorage_.at(ownSmartsConfNum_).writer);
    // if (ownSmartsConfNum_ == smartStageThreeStorage_.at(ownSmartsConfNum_).writer) {
    auto& conv = cs::Conveyer::instance();

    for (auto& st : smartStageThreeStorage_) {
        if (st.sender != cs::ConfidantConsts::InvalidConfidantIndex) {
            if (finalSmartTransactionPack_.addSignature(st.sender, st.packageSignature)) {
                csdebug() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "} signature of T[" << static_cast<int>(st.sender)
                          << "] added to the Transactions Packet";
            }
            else {
                csdebug() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "} signature of T[" << static_cast<int>(st.sender) << "] isn't added";
            }
        }
    }
    csdebug() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "} adding separate package with " << finalSmartTransactionPack_.signatures().size()
              << " signatures";
    conv.addSeparatePacket(finalSmartTransactionPack_);

    // TODO:
    size_t fieldsNumber = finalSmartTransactionPack_.transactions().at(0).user_field_ids().size();
    csdetails() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "} transaction user fields = " << fieldsNumber;
    csdebug() << kLogPrefix << '{' << smartRoundNumber_ << '.' << smartTransaction_ << "} ==============================================> TRANSACTION SENT TO CONVEYER";
}

void SmartConsensus::gotSmartStageRequest(uint8_t msgType, cs::Sequence smartRound, uint32_t startTransaction, uint8_t requesterNumber, uint8_t requiredNumber,
                                          const cs::PublicKey& requester) {
    if (smartRoundNumber_ != smartRound || smartTransaction_ != startTransaction) {
        return;
    }

    if (!smartConfidantExist(requesterNumber)) {
        return;
    }
    if (smartConfidants().size() <= requesterNumber) {
        // normally unexpected
        return;
    }
    if (requester != smartConfidants().at(requesterNumber)) {
        return;
    }
    // const cs::Bytes message, const cs::RoundNumber smartRNum, const cs::Signature& signature, const MsgTypes msgType, const cs::PublicKey requester
    switch (msgType) {
        case MsgTypes::SmartFirstStageRequest:
            if ((size_t)requiredNumber < smartStageOneStorage_.size()) {
                if (smartStageOneStorage_.at(requiredNumber).sender == cs::ConfidantConsts::InvalidConfidantIndex) {
                    pnode_->smartStageEmptyReply(requesterNumber);
                }
                else {
                    pnode_->sendSmartStageReply(smartStageOneStorage_.at(requiredNumber).message, smartRoundNumber_, smartStageOneStorage_.at(requiredNumber).signature,
                                                MsgTypes::FirstSmartStage, requester);
                }
            }
            break;
        case MsgTypes::SmartSecondStageRequest:
            if ((size_t)requiredNumber < smartStageTwoStorage_.size()) {
                if (smartStageTwoStorage_.at(requiredNumber).sender == cs::ConfidantConsts::InvalidConfidantIndex) {
                    pnode_->smartStageEmptyReply(requesterNumber);
                }
                else {
                    pnode_->sendSmartStageReply(smartStageTwoStorage_.at(requiredNumber).message, smartRoundNumber_, smartStageTwoStorage_.at(requiredNumber).signature,
                                                MsgTypes::FirstSmartStage, requester);
                }
            }
            break;
        case MsgTypes::SmartThirdStageRequest:
            if ((size_t)requiredNumber < smartStageThreeStorage_.size()) {
                if (smartStageThreeStorage_.at(requiredNumber).sender == cs::ConfidantConsts::InvalidConfidantIndex) {
                    pnode_->smartStageEmptyReply(requesterNumber);
                }
                else {
                    pnode_->sendSmartStageReply(smartStageThreeStorage_.at(requiredNumber).message, smartRoundNumber_, smartStageThreeStorage_.at(requiredNumber).signature,
                                                MsgTypes::FirstSmartStage, requester);
                }
            }
            break;
    }
}

bool SmartConsensus::smartStageOneEnough() {
    return smartStageEnough(smartStageOneStorage_, "SmartStageOne");
}

bool SmartConsensus::smartStageTwoEnough() {
    return smartStageEnough(smartStageTwoStorage_, "SmartStageTwo");
}

bool SmartConsensus::smartStageThreeEnough() {
    return smartStageEnough(smartStageThreeStorage_, "SmartStageThree");
}

template <class T>
bool SmartConsensus::smartStageEnough(const std::vector<T>& smartStageStorage, const std::string& funcName) {
    size_t stageSize = 0;
    for (size_t idx = 0; idx < smartStageStorage.size(); ++idx) {
        if (smartStageStorage[idx].sender == idx) {
            ++stageSize;
        }
    }
    size_t cSize;
    if (funcName == "SmartStageThree") {
        cSize = smartConfidants_.size() / 2 + 1;
    }
    else {
        cSize = smartConfidants_.size();
    }
    csdebug() << kLogPrefix << "{" << smartRoundNumber_ << '.' << smartTransaction_ << "} " << funcName << " completed " << stageSize << " of " << cSize;
    return stageSize == cSize;
}

void SmartConsensus::startTimer(int st) {
    csdebug() << kLogPrefix << "start track timeout " << Consensus::T_stage_request << " ms of stages-" << st << " received";
    timeoutStageCounter_ = st;
    timeout_request_stage.start(
        psmarts_->getScheduler(), Consensus::T_stage_request,
        // timeout #1 handler:
        [this, st]() {
            csdebug() << kLogPrefix << "(): timeout for stages-" << st << " is expired, make requests";
            requestSmartStages(st);
            // start subsequent track timeout for "wide" request
            csdebug() << kLogPrefix << "(): start subsequent track timeout " << Consensus::T_stage_request << " ms to request neighbors about stages-" << st;
            timeout_request_neighbors.start(psmarts_->getScheduler(), Consensus::T_stage_request,
                                            // timeout #2 handler:
                                            [this, st]() {
                                                csdebug() << kLogPrefix << "(): timeout for requested stages-" << st << " is expired, make requests to neighbors";
                                                requestSmartStagesNeighbors(st);
                                                // timeout #3 handler
                                                timeout_force_transition.start(psmarts_->getScheduler(), Consensus::T_stage_request,
                                                                               [this, st]() {
                                                                                   csdebug() << kLogPrefix
                                                                                             << "(): timeout for transition is expired, mark silent nodes as no stage-" << st;
                                                                                   markSmartOutboundNodes(st);
                                                                               },
                                                                               true /*replace if exists*/, timer_tag());
                                            },
                                            true /*replace if exists*/, timer_tag());
        },
        true /*replace if exists*/, timer_tag());
}

void SmartConsensus::killTimer() {
    if (timeout_request_stage.cancel()) {
        csdebug() << kLogPrefix << __func__ << "(): cancel track timeout of stages-" << timeoutStageCounter_;
    }
    if (timeout_request_neighbors.cancel()) {
        csdebug() << kLogPrefix << __func__ << "(): cancel track timeout to request neighbors about stages-" << timeoutStageCounter_;
    }
    if (timeout_force_transition.cancel()) {
        csdebug() << kLogPrefix << __func__ << "(): cancel track timeout to force transition to next state after stages-" << timeoutStageCounter_;
    }
}

void SmartConsensus::requestSmartStages(int st) {
    csmeta(csdetails);
    uint8_t cnt = static_cast<uint8_t>(smartConfidants_.size());
    bool isRequested = false;
    MsgTypes msg = MsgTypes::SmartFirstStageRequest;
    uint8_t sender = 0;

    for (uint8_t i = 0; i < cnt; ++i) {
        switch (st) {
            case 1:
                sender = smartStageOneStorage_[i].sender;
                msg = MsgTypes::SmartFirstStageRequest;
                break;
            case 2:
                sender = smartStageTwoStorage_[i].sender;
                msg = MsgTypes::SmartSecondStageRequest;
                break;
            case 3:
                sender = smartStageThreeStorage_[i].sender;
                msg = MsgTypes::SmartThirdStageRequest;
                break;
        }

        if (sender == cs::ConfidantConsts::InvalidConfidantIndex) {
            if (i != ownSmartsConfNum_ && i != sender && smartConfidantExist(i)) {
                pnode_->smartStageRequest(msg, smartRoundNumber_, smartTransaction_, smartConfidants_.at(i), ownSmartsConfNum_, i);
            }
            isRequested = true;
        }
    }

    if (!isRequested) {
        csdebug() << kLogPrefix << __func__ << ": no node to request";
    }
}

// requests stages from any available neighbor nodes
void SmartConsensus::requestSmartStagesNeighbors(int st) {
    csmeta(csdetails);
    const uint8_t cnt = static_cast<uint8_t>(smartConfidants_.size());
    bool isRequested = false;
    uint8_t required = 0;
    MsgTypes messageType = MsgTypes::SmartFirstStageRequest;

    for (uint8_t idx = 0; idx < cnt; ++idx) {
        switch (st) {
            case 1:
                required = smartStageOneStorage_[idx].sender;
                messageType = MsgTypes::SmartFirstStageRequest;
                break;
            case 2:
                required = smartStageTwoStorage_[idx].sender;
                messageType = MsgTypes::SmartSecondStageRequest;
                break;
            case 3:
                required = smartStageThreeStorage_[idx].sender;
                messageType = MsgTypes::SmartThirdStageRequest;
                break;
        }

        if (required == cs::ConfidantConsts::InvalidConfidantIndex) {
            if (idx != ownSmartsConfNum_ && idx != required && smartConfidantExist(idx)) {
                pnode_->smartStageRequest(messageType, smartRoundNumber_, smartTransaction_, smartConfidants_.at(idx), ownSmartsConfNum_, required);
                isRequested = true;
            }
        }
    }

    if (!isRequested) {
        csdebug() << kLogPrefix << __func__ << ": no node to request";
    }
}

// forces transition to next stage
void SmartConsensus::markSmartOutboundNodes(int st) {
    uint8_t count = 0;
    switch (st) {
        case 1:

            for (auto& it : smartStageOneStorage_) {
                if (it.sender == cs::ConfidantConsts::InvalidConfidantIndex) {
                    fake_stage1(count);
                }
                ++count;
            }
            return;
        case 2:

            for (auto& it : smartStageTwoStorage_) {
                if (it.sender == cs::ConfidantConsts::InvalidConfidantIndex) {
                    fake_stage2(count);
                }
                ++count;
            }
            return;
        case 3:
            for (auto& it : smartStageThreeStorage_) {
                if (it.sender == cs::ConfidantConsts::InvalidConfidantIndex) {
                    st3.realTrustedMask[count] = cs::ConfidantConsts::InvalidConfidantIndex;
                    trustedChanged_ = true;
                }
                ++count;
            }
            if (trustedChanged_) {
                smartStageThreeStorage_.clear();
                smartStageThreeStorage_.resize(st3.realTrustedMask.size());
                smartStageThreeTempStorage_.clear();
                ++(st3.iteration);
            }
            return;
    }
}

void SmartConsensus::fake_stage1(uint8_t from) {
    bool find = false;
    for (auto& it : smartStageOneStorage_) {
        if (it.sender == from) {
            find = true;
            break;
        }
    }
    if (!find) {
        csdebug() << kLogPrefix << "make stage-1 [" << static_cast<int>(from) << "] as silent";
        cs::StageOneSmarts fake;
        init_zero(fake);
        fake.sender = from;
        fake.id = id();
        addSmartStageOne(fake, false);
    }
}

void SmartConsensus::fake_stage2(uint8_t from) {
    bool find = false;
    for (auto& it : smartStageTwoStorage_) {
        if (it.sender == from) {
            find = true;
            break;
        }
    }
    if (!find) {
        csdebug() << kLogPrefix << "make stage-2 [" << static_cast<int>(from) << "] as silent";
        cs::StageTwoSmarts fake;
        init_zero(fake);
        fake.sender = from;
        fake.id = id();
        addSmartStageTwo(fake, false);
    }
}

void SmartConsensus::init_zero(cs::StageOneSmarts& stage) {
    stage.sender = cs::ConfidantConsts::InvalidConfidantIndex;
    stage.hash = Zero::hash;
    stage.messageHash = Zero::hash;
    stage.signature = Zero::signature;
}

void SmartConsensus::init_zero(cs::StageTwoSmarts& stage) {
    stage.sender = cs::ConfidantConsts::InvalidConfidantIndex;
    stage.signature = Zero::signature;
    size_t cnt = smartConfidants_.size();
    stage.hashes.resize(cnt, Zero::hash);
    stage.signatures.resize(cnt, Zero::signature);
}

void SmartConsensus::fakeStage(uint8_t confIndex) {
    csunused(confIndex);
}

bool SmartConsensus::smartConfidantExist(uint8_t confidantIndex) {
    return confidantIndex < smartConfidants_.size();
}

/*static*/
void SmartConsensus::sendFakeStageOne(Node* pnode, cs::PublicKeys confidants, cs::Byte confidantIndex, uint64_t smartId) {
    cs::StageOneSmarts fake;
    fake.sender = confidantIndex;
    fake.hash.fill(0);
    fake.id = smartId;
    pnode->sendSmartStageOne(confidants, fake);
}

/*static*/
void SmartConsensus::sendFakeStageTwo(Node* pnode, cs::PublicKeys confidants, cs::Byte confidantIndex, uint64_t smartId) {
    csunused(smartId);
    cs::StageTwoSmarts fake;
    fake.sender = confidantIndex;
    size_t cnt = confidants.size();
    cs::Hash zHash;
    cs::Signature zSignature;
    zHash.fill(0);
    zSignature.fill(0);
    fake.hashes.resize(cnt, zHash);
    fake.signatures.resize(cnt, zSignature);
    pnode->sendSmartStageTwo(confidants, fake);
}

}  // namespace cs
