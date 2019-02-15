#include <smartconsensus.hpp>
#include <smartcontracts.hpp>

#pragma warning(push)
#pragma warning(disable : 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)

#include <csnode/datastream.hpp>
#include <solvercore.hpp>

namespace {
  const char *log_prefix = "Smart: ";
}

namespace cs{

  SmartConsensus::SmartConsensus(){
    pnode_ = nullptr;
    psmarts_ = nullptr;
  }

  SmartConsensus::~SmartConsensus() {
    cslog() << "======================  SMART-ROUND " << smartRoundNumber_ << " END =====================";
    pnode_->removeSmartConsensus(this->smartAddress_);
    cs::Connector::disconnect(&pnode_->gotSmartStageOne, this, &cs::SmartConsensus::addSmartStageOne);
    cs::Connector::disconnect(&pnode_->gotSmartStageTwo, this, &cs::SmartConsensus::addSmartStageTwo);
    cs::Connector::disconnect(&pnode_->gotSmartStageThree, this, &cs::SmartConsensus::addSmartStageThree);
    cs::Connector::disconnect(&pnode_->receivedSmartStageRequest, this, &cs::SmartConsensus::gotSmartStageRequest);
  }

  const std::vector<cs::PublicKey>& SmartConsensus::smartConfidants() const {
    return smartConfidants_;
  }

  bool SmartConsensus::initSmartRound(const cs::TransactionsPacket& pack, Node* node, SmartContracts* smarts) {
    csdebug() << log_prefix << "starting ... ";
    pnode_ = node;
    psmarts_ = smarts;
    smartConfidants_.clear();
    smartRoundNumber_ = 0;
	  csdb::Address abs_addr;
    for (const auto& tr : pack.transactions()) {
      if (SmartContracts::is_new_state(tr)) {
        abs_addr = smarts->absolute_address(tr.source());
        csdb::UserField fld = tr.user_field(trx_uf::new_state::RefStart);
        if (fld.is_valid()) {
          SmartContractRef ref(fld);
          if (ref.is_valid()) {
            smartRoundNumber_ = ref.sequence;
          }
        }
        break;
      }
    }
    if (0 == smartRoundNumber_ || !abs_addr.is_valid()) {
      cserror() << log_prefix << "smart contract result packet must contain new state transaction";
      return false;
    }
    smartConfidants_ = pnode_->retriveSmartConfidants(smartRoundNumber_);
    ownSmartsConfNum_ = calculateSmartsConfNum();
    refreshSmartStagesStorage();
    if (ownSmartsConfNum_ == cs::InvalidConfidantIndex) {
      cserror() << log_prefix << "cannot determine own number in confidant list";
      return false;
    }

    cslog() << "======================  SMART-ROUND: " << smartRoundNumber_ << " [" << static_cast<int>(ownSmartsConfNum_)
      << "] =========================";
    csdebug() << log_prefix << "SMART confidants (" << smartConfidants_.size() << "):";

    // cscrypto::CalculateHash(st1.hash,transaction.to_byte_stream().data(), transaction.to_byte_stream().size());
    currentSmartTransactionPack_ = pack;
    currentSmartTransactionPack_.makeHash();
    auto tmp = currentSmartTransactionPack_.hash().toBinary();
    std::copy(tmp.cbegin(), tmp.cend(), st1.hash.begin());
    st1.smartAddress = smartAddress_ = abs_addr.public_key();
    // signals subscription
    cs::Connector::connect(&pnode_->gotSmartStageOne, this, &cs::SmartConsensus::addSmartStageOne);
    cs::Connector::connect(&pnode_->gotSmartStageTwo, this, &cs::SmartConsensus::addSmartStageTwo);
    cs::Connector::connect(&pnode_->gotSmartStageThree, this, &cs::SmartConsensus::addSmartStageThree);
    cs::Connector::connect(&pnode_->receivedSmartStageRequest, this, &cs::SmartConsensus::gotSmartStageRequest);
    pnode_->addSmartConsensus(smartAddress_);
    st1.sender = ownSmartsConfNum_;
    st1.sRoundNum = smartRoundNumber_;
    addSmartStageOne(st1, true);
    return true;
  }

  uint8_t SmartConsensus::calculateSmartsConfNum()
  {
    uint8_t i = 0;
    uint8_t ownSmartConfNumber = cs::InvalidConfidantIndex;
    for (auto& e : smartConfidants_) {
      if (e == pnode_->getNodeIdKey()) {
        ownSmartConfNumber = i;
      }
      csdebug() << log_prefix << "[" << static_cast<int>(i) << "] "
        << (ownSmartConfNumber != cs::InvalidConfidantIndex && i == ownSmartConfNumber
          ? "me"
          : cs::Utils::byteStreamToHex(e.data(), e.size()));
      ++i;
    }

    if (ownSmartConfNumber == cs::InvalidConfidantIndex) {
      csdebug() << log_prefix << "          This NODE is not a confidant one for this smart-contract consensus round";
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
    st3.realTrustedMask.clear();
    st3.realTrustedMask.resize(cSize);
    st3.packageSignature.fill(0);
    st2.sender = cs::ConfidantConsts::InvalidConfidantIndex;
    st3.sender = cs::ConfidantConsts::InvalidConfidantIndex;
    st3.writer = cs::ConfidantConsts::InvalidConfidantIndex;
    st2.sRoundNum = 0;
    st3.sRoundNum = 0;

    memset(st3.signature.data(), 0, st3.signature.size());
    memset(st2.signature.data(), 0, st3.signature.size());

    //smartStagesStorageClear(cSize);

    smartUntrusted.clear();
    smartUntrusted.resize(cSize);

    std::fill(smartUntrusted.begin(), smartUntrusted.end(), 0);
    //startTimer(1);
  }

  void SmartConsensus::addSmartStageOne(cs::StageOneSmarts& stage, bool send) {
    csmeta(csdetails) << "start";
    if (send) {
      pnode_->sendSmartStageOne(smartConfidants_, stage);
    }
    if (smartStageOneStorage_.at(stage.sender).sender == stage.sender) {
      return;
    }
    smartStageOneStorage_.at(stage.sender) = stage;
    for (size_t i = 0; i < smartConfidants_.size(); ++i) {
      csdebug() << log_prefix << "[" << i << "] - " << static_cast<int>(smartStageOneStorage_.at(i).sender);
    }
    csdebug() << log_prefix << " <-- SMART-Stage-1 [" << static_cast<int>(stage.sender) << "]";
    st2.signatures.at(stage.sender) = stage.signature;
    st2.hashes.at(stage.sender) = stage.messageHash;
    if (smartStageOneEnough()) {
      //killTimer(1);
      cs::Connector::disconnect(&pnode_->gotSmartStageOne, this, &cs::SmartConsensus::addSmartStageOne);
      addSmartStageTwo(st2, true);
      //startTimer(2);
    }
  }

  void SmartConsensus::addSmartStageTwo(cs::StageTwoSmarts& stage, bool send) {
    if (send) {
      st2.sender = ownSmartsConfNum_;
      st2.sRoundNum = smartRoundNumber_;
      pnode_->sendSmartStageTwo(smartConfidants_, stage);
    }
    auto& stageTwo = smartStageTwoStorage_.at(stage.sender);
    if (stageTwo.sender == stage.sender) {
      return;
    }
    stageTwo = stage;
    csdebug() << log_prefix << " <-- SMART-Stage-2 [" << static_cast<int>(stage.sender) << "] = " << smartStageTwoStorage_.size();
    if (smartStageTwoEnough()) {
      //startTimer(2);
      cs::Connector::disconnect(&pnode_->gotSmartStageTwo, this, &cs::SmartConsensus::addSmartStageTwo);
      processStages();
    }
  }

  cs::PublicKey SmartConsensus::smartAddress() {
    return smartAddress_;
  }

  void SmartConsensus::processStages() {
    csmeta(csdetails) << "start";
    const size_t cnt = smartConfidants_.size();
    //perform the evaluation og stages 1 & 2 to find out who is traitor
    int hashFrequency = 1;
    const auto& hash_t = smartStageOneStorage_.at(ownSmartsConfNum_).hash;
    for (auto& st : smartStageOneStorage_) {
      if (st.sender == ownSmartsConfNum_) {
        continue;
      }
      if (st.hash != hash_t) {
        ++(smartUntrusted.at(st.sender));
        cslog() << "Confidant [" << static_cast<int>(st.sender) << "] is markt as untrusted (wrong hash)";
      }
      else {
        ++hashFrequency;
      }
    }
    csdebug() << log_prefix << "Hash " << cs::Utils::byteStreamToHex(hash_t.data(), hash_t.size())
      << ", Frequency = " << hashFrequency;
    auto& myStage2 = smartStageTwoStorage_.at(ownSmartsConfNum_);
    for (auto& st : smartStageTwoStorage_) {
      if (st.sender == ownSmartsConfNum_) {
        continue;
      }
      for (size_t i = 0; i < cnt; ++i) {
        if (st.signatures[i] != myStage2.signatures[i]) {
          if (cscrypto::verifySignature(st.signatures[i], smartConfidants_[i], st.hashes[i].data(), sizeof(st.hashes[i]))) {
            ++(smartUntrusted.at(i));
            cslog() << "Confidant [" << i << "] is marked as untrusted (wrong hash)";
          }
          else {
            ++(smartUntrusted.at(st.sender));
            cslog() << "Confidant [" << static_cast<int>(st.sender) << "] is marked as untrusted (wrong signature)";
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
      cslog() << log_prefix << "smart consensus is NOT achieved, the state transaction won't send to the conveyer";
      return;
    }
    csdebug() << log_prefix << "smart consensus achieved";

    if (hash_t.empty()) {
      return;  // TODO: decide what to return
    }
    int k = *(unsigned int *)hash_t.data();
    if (k < 0) {
      k = -k;
    }
    csdebug() << log_prefix << "smart consensus result 1 from 3";
    size_t idx_writer = static_cast<size_t>(k % cnt_active);
    size_t idx = 0;
    for (size_t i = 0; i < cnt; ++i) {
      if (stage.realTrustedMask.at(i) != InvalidConfidantIndex) {
        if (idx == idx_writer) {
          stage.writer = static_cast<uint8_t>(i);
        }
        ++idx;
      }
    }
    csdebug() << log_prefix << "smart consensus result 2 from 3";
    idx = 0;
    for (size_t i = stage.writer; i < cnt + stage.writer; ++i) {
      size_t c = i % cnt;
      if (stage.realTrustedMask.at(c) != InvalidConfidantIndex) {
        stage.realTrustedMask.at(c) = static_cast<uint8_t>(idx);
        ++idx;
      }
    }
    csdebug() << log_prefix << "smart consensus result 3 from 3";
    //startTimer(3);
    stage.packageSignature = cscrypto::generateSignature(pnode_->getSolver()->getPrivateKey(), hash_t.data(), hash_t.size());
    csmeta(csdebug) << "done";
    addSmartStageThree(stage, true);
  }

  void SmartConsensus::addSmartStageThree(cs::StageThreeSmarts& stage, bool send) {
    csmeta(csdetails);
    if (send) {
      csdebug() << log_prefix << "____ 1.";
      stage.sender = ownSmartsConfNum_;
      stage.sRoundNum = smartRoundNumber_;
      pnode_->sendSmartStageThree(smartConfidants_, stage);
    }
    if (smartStageThreeStorage_.at(stage.sender).sender == stage.sender) {
      return;
    }
    if (stage.sender != ownSmartsConfNum_) {
      const auto& hash = smartStageOneStorage_.at(stage.sender).hash;
      if (!cscrypto::verifySignature(stage.packageSignature, smartConfidants().at(stage.sender), hash.data(), hash.size())) {
        cslog() << log_prefix << "____ The signature is not valid";
        return; //returns this function of the signature of smart confidant is not corresponding to its the previously sent hash
      }
    }

    smartStageThreeStorage_.at(stage.sender) = stage;
    const auto smartStorageSize = std::count_if(smartStageThreeStorage_.begin(), smartStageThreeStorage_.end(),
      [](const cs::StageThreeSmarts& it) {
      return it.sender != cs::ConfidantConsts::InvalidConfidantIndex;
    });
    csdebug() << log_prefix << ": <-- SMART-Stage-3 [" << static_cast<int>(stage.sender) << "] = " << smartStorageSize;
    if (smartStageThreeEnough()) {
      //killTimer(3);
      cs::Connector::disconnect(&pnode_->gotSmartStageThree, this, &cs::SmartConsensus::addSmartStageThree);
      createFinalTransactionSet();
    }
  }

  void SmartConsensus::createFinalTransactionSet() {
    csmeta(csdetails) << "<starting> ownSmartConfNum = " << static_cast<int>(ownSmartsConfNum_)
      << ", writer = " << static_cast<int>(smartStageThreeStorage_.at(ownSmartsConfNum_).writer);
    //if (ownSmartsConfNum_ == smartStageThreeStorage_.at(ownSmartsConfNum_).writer) {
    auto& conv = cs::Conveyer::instance();

    for (auto& st : smartStageThreeStorage_) {
      if (st.sender != cs::ConfidantConsts::InvalidConfidantIndex) {
        if (currentSmartTransactionPack_.addSignature(st.sender, st.packageSignature)) {
          csdebug() << log_prefix << "signature of T[" << static_cast<int>(st.sender) << "] added to the Transactions Packet";
        } 
        else{
          csdebug() << log_prefix << "signature of T[" << static_cast<int>(st.sender) << "] isn't added";
        }
      }
    }
    csdebug() << log_prefix << "adding separate package with " << currentSmartTransactionPack_.signatures().size() << " signatures";
    conv.addSeparatePacket(currentSmartTransactionPack_);

    // TODO: 
    size_t fieldsNumber = currentSmartTransactionPack_.transactions().at(0).user_field_ids().size();
    csdetails() << log_prefix << "transaction user fields = " << fieldsNumber;
    csdebug() << log_prefix << "==============================================> TRANSACTION SENT TO CONVEYER";
  }

  void SmartConsensus::gotSmartStageRequest(uint8_t msgType, cs::PublicKey smartAddress
      , uint8_t requesterNumber, uint8_t requiredNumber, cs::PublicKey& requester) {
    
    if (smartAddress_ != smartAddress) {
      return;
    }

    if (!smartConfidantExist(requesterNumber)) {
      return;
    }
    if (requester != smartConfidants().at(requesterNumber)) {
      return;
    }
    //const cs::Bytes message, const cs::RoundNumber smartRNum, const cs::Signature& signature, const MsgTypes msgType, const cs::PublicKey requester
    switch (msgType) {
    case MsgTypes::SmartFirstStageRequest:
      if (smartStageOneStorage_.at(requiredNumber).sender == cs::ConfidantConsts::InvalidConfidantIndex) {
        pnode_->smartStageEmptyReply(requesterNumber);
      }
      pnode_->sendSmartStageReply(smartStageOneStorage_.at(requiredNumber).message, smartStageOneStorage_.at(requiredNumber).sRoundNum
          , smartStageOneStorage_.at(requiredNumber).signature, MsgTypes::FirstSmartStage, requester);
      break;
    case MsgTypes::SmartSecondStageRequest:
      if (smartStageTwoStorage_.at(requiredNumber).sender == cs::ConfidantConsts::InvalidConfidantIndex) {
        pnode_->smartStageEmptyReply(requesterNumber);
      }
      pnode_->sendSmartStageReply(smartStageTwoStorage_.at(requiredNumber).message, smartStageTwoStorage_.at(requiredNumber).sRoundNum
        , smartStageTwoStorage_.at(requiredNumber).signature, MsgTypes::FirstSmartStage, requester);
      break;
    case MsgTypes::SmartThirdStageRequest:
      if (smartStageThreeStorage_.at(requiredNumber).sender == cs::ConfidantConsts::InvalidConfidantIndex) {
        pnode_->smartStageEmptyReply(requesterNumber);
      }
      pnode_->sendSmartStageReply(smartStageThreeStorage_.at(requiredNumber).message, smartStageThreeStorage_.at(requiredNumber).sRoundNum
        , smartStageThreeStorage_.at(requiredNumber).signature, MsgTypes::FirstSmartStage, requester);
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

  template<class T>
  bool SmartConsensus::smartStageEnough(const std::vector<T>& smartStageStorage, const std::string& funcName) {
    size_t stageSize = 0;
    for (size_t idx = 0; idx < smartStageStorage.size(); ++idx) {
      if (smartStageStorage[idx].sender == idx) {
        ++stageSize;
      }
    }
    size_t cSize;
    if(funcName == "SmartStageThree") {
      cSize = smartConfidants_.size() / 2 + 1;
    }
    else {
      cSize = smartConfidants_.size();
    }
    csdebug() << log_prefix << "        " << funcName << " completed " << stageSize << " of " << cSize;
    return stageSize == cSize;
  }

  void SmartConsensus::startTimer(int /*st*/)
  {
    //csmeta(csdetails) << "start track timeout " << Consensus::T_stage_request << " ms of stages-" << st << " received";
    //timeout_request_stage.start(
    //  psmarts_->getScheduler(), Consensus::T_stage_request,
    //  // timeout #1 handler:
    //  [this, st]() {
    //  csdebug() << __func__ << "(): timeout for stages-" << st << " is expired, make requests";
    //  requestSmartStages(st);
    //  // start subsequent track timeout for "wide" request
    //  csdebug() << __func__ << "(): start subsequent track timeout " << Consensus::T_stage_request
    //    << " ms to request neighbors about stages-" << st;
    //  timeout_request_neighbors.start(
    //    psmarts_->getScheduler(), Consensus::T_stage_request,
    //    // timeout #2 handler:
    //    [this, st]() {
    //    csdebug() << __func__ << "(): timeout for requested stages is expired, make requests to neighbors";
    //    requestSmartStagesNeighbors(st);
    //    // timeout #3 handler
    //    timeout_force_transition.start(
    //      psmarts_->getScheduler(), Consensus::T_stage_request,
    //      [this, st]() {
    //      csdebug() << __func__ << "(): timeout for transition is expired, mark silent nodes as outbound";
    //      markSmartOutboundNodes();
    //    },
    //      true/*replace if exists*/);
    //  },
    //    true /*replace if exists*/);
    //},
    //  true /*replace if exists*/);
  }

  void SmartConsensus::killTimer(int st) {
    if (timeout_request_stage.cancel()) {
      csdebug() << log_prefix << __func__ << "(): cancel track timeout of stages-" << st;
    }
    if (timeout_request_neighbors.cancel()) {
      csdebug() << log_prefix << __func__ << "(): cancel track timeout to request neighbors about stages-" << st;
    }
    if (timeout_force_transition.cancel()) {
      csdebug() << log_prefix << __func__ << "(): cancel track timeout to force transition to next state";
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
          pnode_->smartStageRequest(msg, smartAddress_, smartConfidants_.at(i), ownSmartsConfNum_, i);
        }
        isRequested = true;
      }
    }

    if (!isRequested) {
      csdebug() << log_prefix << __func__ << ": no node to request";
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
          pnode_->smartStageRequest(messageType, smartAddress_, smartConfidants_.at(idx), ownSmartsConfNum_, required);
          isRequested = true;
        }
      }
    }

    if (!isRequested) {
      csdebug() << log_prefix << __func__ << ": no node to request";
    }
  }

  // forces transition to next stage
  void SmartConsensus::markSmartOutboundNodes()
  {
    //uint8_t cnt = (uint8_t)smartConfidants_.size();
    //for (uint8_t i = 0; i < cnt; ++i) {
    //  if (context.stage1(i) == nullptr) {
    //    // it is possible to get a transition to other state in SmartConsensus from any iteration, this is not a problem, simply execute method until end
    //    fake_stage1(i);
    //  }
    //}
  }

  void SmartConsensus::fakeStage(uint8_t confIndex) {
    csunused(confIndex);
  }

  bool SmartConsensus::smartConfidantExist(uint8_t confidantIndex) {
    return confidantIndex < smartConfidants_.size();
  }

}
