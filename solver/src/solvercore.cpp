#include <smartcontracts.hpp>
#include <solvercontext.hpp>
#include <solvercore.hpp>
#include <states/nostate.hpp>

#pragma warning(push)
#pragma warning(disable : 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)

#include <csnode/spammer.hpp>
#include <csnode/walletsstate.hpp>

#include <lib/system/logger.hpp>

#include <functional>
#include <limits>
#include <sstream>
#include <string>

namespace cs {

// initial values for SolverCore options

// To track timeout for active state
constexpr const bool TimeoutsEnabled = false;
// To enable make a transition to the same state
constexpr const bool RepeatStateEnabled = true;
// Special mode: uses debug transition table
constexpr const bool DebugModeOn = false;
// Special mode: uses monitor mode transition table
constexpr const bool MonitorModeOn =
#if defined(MONITOR_NODE)
    true;
#else
    false;
#endif  // MONITOR_NODE

constexpr const bool WebWalletModeOn =
#if defined(WEB_WALLET_NODE) && false
    true;
#else
    false;
#endif  // WEB_WALLET_NODE

// default (test intended) constructor
SolverCore::SolverCore()
// options
: opt_timeouts_enabled(TimeoutsEnabled)
, opt_repeat_state_enabled(RepeatStateEnabled)
, opt_mode(Mode::Default)
// inner data
, pcontext(std::make_unique<SolverContext>(*this))
, tag_state_expired(CallsQueueScheduler::no_tag)
, req_stop(true)
, cnt_trusted_desired(Consensus::MinTrustedNodes)
// consensus data
, cur_round(0)
// previous solver version instance
, pnode(nullptr)
, pws(nullptr)
, psmarts(nullptr) {
  if constexpr (MonitorModeOn) {
    cslog() << "SolverCore: opt_monitor_mode is on, so use special transition table";
    InitMonitorModeTransitions();
  }
  else if constexpr (WebWalletModeOn) {
    cslog() << "SolverCore: opt_web_wallet_mode is on, so use special transition table";
    InitWebWalletModeTransitions();
  }
  else if constexpr (DebugModeOn) {
    cslog() << "SolverCore: opt_debug_mode is on, so use special transition table";
    InitDebugModeTransitions();
  }
  else if constexpr (true) {
    cslog() << "SolverCore: use default transition table";
    InitTransitions();
  }
}

  // actual constructor
  SolverCore::SolverCore(Node* pNode, csdb::Address GenesisAddress, csdb::Address StartAddress)
    : SolverCore() {
    addr_genesis = GenesisAddress;
    addr_start = StartAddress;
    pnode = pNode;
    auto& bc = pNode->getBlockChain();
    pws = std::make_unique<cs::WalletsState>(bc);
    psmarts = std::make_unique<cs::SmartContracts>(bc, scheduler);
    // bind signals
    cs::Connector::connect(&psmarts->signal_smart_executed, this, &cs::SolverCore::getSmartResult);
  }

SolverCore::~SolverCore() {
  scheduler.Stop();
  transitions.clear();
}

void SolverCore::ExecuteStart(Event start_event) {
  if (!is_finished()) {
    cswarning() << "SolverCore: cannot start again, already started";
    return;
  }
  req_stop = false;
  handleTransitions(start_event);
}

void SolverCore::finish() {
  if (pstate) {
    pstate->off(*pcontext);
  }
  scheduler.RemoveAll();
  tag_state_expired = CallsQueueScheduler::no_tag;
  pstate = std::make_shared<NoState>();
  req_stop = true;
}

void SolverCore::setState(const StatePtr& pState) {
  if (!opt_repeat_state_enabled) {
    if (pState == pstate) {
      return;
    }
  }
  if (tag_state_expired != CallsQueueScheduler::no_tag) {
    // no timeout, cancel waiting
    scheduler.Remove(tag_state_expired);
    tag_state_expired = CallsQueueScheduler::no_tag;
  }
  else {
    // state changed due timeout from within expired state
  }

  if (pstate) {
    pstate->off(*pcontext);
  }
  if (Consensus::Log) {
    csdebug() << "SolverCore: switch " << (pstate ? pstate->name() : "null") << " -> "
            << (pState ? pState->name() : "null");
  }
  pstate = pState;
  if (!pstate) {
    return;
  }
  pstate->on(*pcontext);

  auto closure = [this]() {
    csdebug() << "SolverCore: state " << pstate->name() << " is expired";
    // clear flag to know timeout expired
    tag_state_expired = CallsQueueScheduler::no_tag;
    // control state switch
    std::weak_ptr<INodeState> p1(pstate);
    pstate->expired(*pcontext);
    if (pstate == p1.lock()) {
      // expired state did not change to another one, do it now
      csdebug() << "SolverCore: there is no state set on expiration of " << pstate->name();
      // setNormalState();
    }
  };

  // timeout handling
  if (opt_timeouts_enabled) {
    tag_state_expired = scheduler.InsertOnce(Consensus::DefaultStateTimeout, closure,true /*replace if exists*/);
  }
}

void SolverCore::handleTransitions(Event evt) {
  if (!pstate) {
    // unable to work until initTransitions() called
    return;
  }
  if (Event::BigBang == evt) {
    cswarning() << "SolverCore: BigBang on";
  }
  const auto& variants = transitions[pstate];
  if (variants.empty()) {
    cserror() << "SolverCore: there are no transitions for " << pstate->name();
    return;
  }
  auto it = variants.find(evt);
  if (it == variants.cend()) {
    // such event is ignored in current state
    csdebug() << "SolverCore: event " << static_cast<int>(evt) << " ignored in state " << pstate->name();
    return;
  }
  setState(it->second);
}

bool SolverCore::stateCompleted(Result res) {
  if (Result::Failure == res) {
    cserror() << "SolverCore: error in state " << (pstate ? pstate->name() : "null");
  }
  return (Result::Finish == res);
}

void SolverCore::spawn_next_round(const std::vector<cs::PublicKey>& nodes, const std::vector<cs::TransactionsPacketHash>& hashes, std::string&& currentTimeStamp) {
  csdebug() << "SolverCore: TRUSTED -> WRITER, do write & send block";

  pnode->becomeWriter();

  cs::RoundTable table;
  table.round = cs::Conveyer::instance().currentRoundNumber() + 1;
  table.confidants = nodes;
  table.hashes = hashes;

  csdebug() << "Applying " << hashes.size() << " hashes to ROUND Table";
  for (std::size_t i = 0; i < hashes.size(); ++i) {
    csdetails() << '\t' << i << ". " << hashes[i].toString();
  }

  pnode->prepareMetaForSending(table, currentTimeStamp);
}

//smart-part begin VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
  const std::vector<cs::PublicKey>& SolverCore::smartConfidants() const {
    return smartConfidants_;
  }

  void SolverCore::getSmartResult(cs::TransactionsPacket pack) {
    if(pack.transactionsCount() == 0) {
      //TODO: fix failure of smart execution, clear it from exe_queue
      cserror() << "SolverCore: empty packet must not finish smart contract execution";
      return;
    }
    smartConfidants_.clear();
    smartRoundNumber_ = 0;
    for(const auto tr : pack.transactions()) {
      if(psmarts->is_new_state(tr)) {
        cs::SmartContractRef smartRef;
        smartRef.from_user_field(tr.user_field(trx_uf::new_state::RefStart));
        smartRoundNumber_ = smartRef.sequence;
      }
    }
    if(0 == smartRoundNumber_) {
      //TODO: fix failure of smart execution, clear it from exe_queue
      cserror() << "SolverCore: smart contract result packet must contain new state transaction";
      return;
    }
    pnode->retriveSmartConfidants(smartRoundNumber_ , smartConfidants_);
    ownSmartsConfNum_ = calculateSmartsConfNum();

    cslog() << "======================  SMART-ROUND: "<< smartRoundNumber_  << " [" << (int)ownSmartsConfNum_ << "] =========================";
    csdebug() << "SMART confidants (" << smartConfidants_.size() << "):";
    refreshSmartStagesStorage();
    if (ownSmartsConfNum_ == cs::InvalidConfidantIndex) {
      return;
    }
    //cscrypto::CalculateHash(st1.hash,transaction.to_byte_stream().data(), transaction.to_byte_stream().size());
    pack.makeHash();
    auto tmp = pack.hash().toBinary();
    std::copy(tmp.cbegin(),tmp.cend(), st1.hash.begin());
    currentSmartTransactionPack_ = pack;
    st1.sender = ownSmartsConfNum_;
    st1.sRoundNum = smartRoundNumber_;
    addSmartStageOne(st1, true);
  }

  uint8_t SolverCore::calculateSmartsConfNum()
  {
    uint8_t i = 0 ;
    uint8_t ownSmartConfNumber = cs::InvalidConfidantIndex;
    for (auto& e : smartConfidants_) {
      if (e == pnode->getNodeIdKey()) {
        ownSmartConfNumber = i;
      }
      csdebug() << "[" << (int)i << "] "
        << (ownSmartConfNumber != cs::InvalidConfidantIndex && i == ownSmartConfNumber
          ? "me"
          : cs::Utils::byteStreamToHex(e.data(), e.size()));
      ++i;
    }
    if (ownSmartConfNumber == cs::InvalidConfidantIndex) {
      csdebug() << "          This NODE is not a confidant one for this smart-contract consensus round";
    }
    return ownSmartConfNumber;
  }

  uint8_t SolverCore::ownSmartsConfidantNumber()
  {
    return ownSmartsConfNum_;
  }

  cs::Sequence SolverCore::smartRoundNumber() {
    return smartRoundNumber_;
  }

  void SolverCore::refreshSmartStagesStorage()
  {
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
    memset(&st1,0,sizeof(st1));
    st2.signatures.clear();
    st2.signatures.resize(cSize);
    st2.hashes.clear();
    st2.hashes.resize(cSize);
    st3.realTrustedMask.clear();
    st3.realTrustedMask.resize(cSize);
    st2.sender = cs::ConfidantConsts::InvalidConfidantIndex;
    st3.sender = cs::ConfidantConsts::InvalidConfidantIndex;
    st3.writer = cs::ConfidantConsts::InvalidConfidantIndex;
    st2.sRoundNum = 0;
    st3.sRoundNum = 0;
    memset(st3.signature.data(),0,st3.signature.size());
    memset(st2.signature.data(),0,st3.signature.size());
    pnode->smartStagesStorageClear(cSize);
    smartUntrusted.clear();
    smartUntrusted.resize(cSize);
    std::fill(smartUntrusted.begin(),smartUntrusted.end(),0);
    startTimer(1);
  }

  void SolverCore::addSmartStageOne( cs::StageOneSmarts& stage, bool send) {
    if (send) {
      pnode->sendSmartStageOne(stage);
    }
    if (smartStageOneStorage_.at(stage.sender).sender == stage.sender) {
      return;
    }
    smartStageOneStorage_.at(stage.sender) = stage;
    for (size_t i=0; i<smartConfidants_.size(); ++i) {
      csdebug() << "[" << i << "] - " << (int)smartStageOneStorage_.at(i).sender;
    }
    csdebug() << "          <-- SMART-Stage-1 [" << (int)stage.sender << "]";
    st2.signatures.at(stage.sender) = stage.signature;
    st2.hashes.at(stage.sender) = stage.messageHash;
    if (smartStageOneEnough()) {
      killTimer(1);
      addSmartStageTwo(st2, true);
      startTimer(2);
    }
  }

  void SolverCore::addSmartStageTwo(cs::StageTwoSmarts& stage, bool send) {
    if (send) {
      st2.sender = ownSmartsConfNum_;
      st2.sRoundNum = smartRoundNumber_;
      pnode->sendSmartStageTwo(stage);
    }
    if (smartStageTwoStorage_.at(stage.sender).sender == stage.sender) {
      return;
    }
    smartStageTwoStorage_.at(stage.sender) = stage;
    csdebug() << ": <-- SMART-Stage-2 [" << (int)stage.sender << "] = " << smartStageTwoStorage_.size();
    if (smartStageTwoEnough()) {
      startTimer(2);
      processStages();
    }
  }

void SolverCore::processStages() {
  csdetails() << __func__ << "(): start";
  int cnt = (int)smartConfidants_.size();
  //perform the evaluation og stages 1 & 2 to find out who is traitor 
  int hashFrequency = 1;
  for (auto& st : smartStageOneStorage_) {
    if (st.sender == ownSmartsConfNum_) {
      continue;
    }
    if (st.hash != smartStageOneStorage_.at(ownSmartsConfNum_).hash) {
      ++(smartUntrusted.at(st.sender));
      cslog() << "Confidant [" << (int)st.sender << "] is markt as untrusted (wrong hash)";
    }
    else {
      ++hashFrequency;
    }
  }
  csdebug() << "Hash: " << cs::Utils::byteStreamToHex(smartStageOneStorage_.at(ownSmartsConfNum_).hash.data(),
    sizeof(smartStageOneStorage_.at(ownSmartsConfNum_).hash)) << ", Frequency = " << hashFrequency;
  auto& myStage2 = smartStageTwoStorage_.at(ownSmartsConfNum_);
  for (auto& st : smartStageTwoStorage_) {
    if(st.sender == ownSmartsConfNum_) {
      continue;
    }
    for (int i = 0; i< cnt; ++i) {
      if (st.signatures[i] != myStage2.signatures[i]) {
        if (cscrypto::VerifySignature(st.signatures[i], smartConfidants_[i], st.hashes[i].data(), sizeof(st.hashes[i]))) {
          ++(smartUntrusted.at(i));
          cslog() << "Confidant [" << i << "] is marked as untrusted (wrong hash)";
        }
        else {
          ++(smartUntrusted.at(st.sender));
          cslog() << "Confidant [" << (int)st.sender << "] is marked as untrusted (wrong signature)";
        }
      }
    }
  }
  int cnt_active = 0;
  cs::StageThreeSmarts stage;
  stage.realTrustedMask.resize(cnt);
  for (int i = 0; i < cnt; ++i) {
    stage.realTrustedMask[i] = (smartUntrusted[i] > 0 ? cs::ConfidantConsts::InvalidConfidantIndex : cs::ConfidantConsts::FirstWriterIndex);
    if (stage.realTrustedMask[i] == cs::ConfidantConsts::FirstWriterIndex) {
      ++cnt_active;
    }
  }
  int lowerTrustedLimit = (int)(smartConfidants_.size() /2. + 1.);
  if (cnt_active < lowerTrustedLimit) {
    cslog() << "Smart's consensus NOT achieved, the state transaction won't send to the conveyer";
    return;    
  }
  csdebug() << "Smart's consensus achieved";

  auto hash_t = smartStageOneStorage_.at(ownSmartsConfNum_).hash;
  if (hash_t.empty()) {
    return;  // TODO: decide what to return
  }
  int k = *(unsigned int *)hash_t.data();
  if (k < 0) {
    k = -k;
  }
  int idx_writer = k % cnt_active;
  int idx = 0;
  int c = 0;
  for (int i = idx_writer; i < cnt + idx_writer; ++i) {
    c = i % cnt;
    if (stage.realTrustedMask.at(c) != cs::ConfidantConsts::InvalidConfidantIndex) {
      stage.realTrustedMask.at(c) = static_cast<uint8_t>(idx);
      if (i == idx_writer) {
        stage.writer = static_cast<uint8_t>(c);
      }
      ++idx;
    }
  }
  startTimer(3);
  csdetails() << __func__ << "(): done";
  addSmartStageThree(stage, true);
}

  void SolverCore::addSmartStageThree(cs::StageThreeSmarts& stage, bool send) {
    csdetails() << __func__;
    if (send) {
      csdebug() << "____ 1.";
      stage.sender = ownSmartsConfNum_;
      stage.sRoundNum = smartRoundNumber_;
      pnode->sendSmartStageThree(stage);
    }
    if (smartStageThreeStorage_.at(stage.sender).sender == stage.sender) {
      return;
    }
    smartStageThreeStorage_.at(stage.sender) = stage;
    csdebug() << ": <-- SMART-Stage-3 [" << (int)stage.sender << "] = " << smartStageThreeStorage_.size();
    if (smartStageThreeEnough()) {
      killTimer(3);
      createFinalTransactionSet();
    }
  }

  void SolverCore::createFinalTransactionSet() {
    csdetails() << __func__ << "(): <starting> ownSmartConfNum = " << (int)ownSmartsConfNum_ << ", writer = " << (int)(smartStageThreeStorage_.at(ownSmartsConfNum_).writer);
    //if (ownSmartsConfNum_ == smartStageThreeStorage_.at(ownSmartsConfNum_).writer) {
      auto& conv = cs::Conveyer::instance();
      
      //for(const auto& tr : currentSmartTransactionPack_.transactions()) {
      //  conv.addTransaction(tr);
      //}
      // alternative is correct but does not work - currentSmartTransactionPack_'s hash must be empty:
      conv.addSeparatePacket(currentSmartTransactionPack_);
      
      size_t fieldsNumber = currentSmartTransactionPack_.transactions().at(0).user_field_ids().size();
      csdetails() << "Transaction user fields = " << fieldsNumber;
      csdebug() << __func__ << "(): ==============================================> TRANSACTION SENT TO CONVEYER";
      return;
    //}
    //csdebug() << __func__ << "(): ==============================================> someone SENT TRANSACTION TO CONVEYER";
  }

  void SolverCore::gotSmartStageRequest(uint8_t msgType, uint8_t requesterNumber, uint8_t requiredNumber) {
    
    switch (msgType) {
      case MsgTypes::SmartFirstStageRequest:
        if (smartStageOneStorage_.at(requiredNumber).sender == cs::ConfidantConsts::InvalidConfidantIndex) {
          pnode->smartStageEmptyReply(requesterNumber);
        }
        pnode->sendSmartStageReply(requiredNumber, smartStageOneStorage_.at(requiredNumber).signature, MsgTypes::FirstSmartStage, requesterNumber);
        break;
      case MsgTypes::SmartSecondStageRequest:
        if (smartStageTwoStorage_.at(requiredNumber).sender == cs::ConfidantConsts::InvalidConfidantIndex) {
          pnode->smartStageEmptyReply(requesterNumber);
        }
        pnode->sendSmartStageReply(requiredNumber, smartStageTwoStorage_.at(requiredNumber).signature, MsgTypes::SecondSmartStage, requesterNumber);
        break;
      case MsgTypes::SmartThirdStageRequest:
        if (smartStageThreeStorage_.at(requiredNumber).sender == cs::ConfidantConsts::InvalidConfidantIndex) {
          pnode->smartStageEmptyReply(requesterNumber);
        }
        pnode->sendSmartStageReply(requiredNumber, smartStageThreeStorage_.at(requiredNumber).signature, MsgTypes::ThirdSmartStage, requesterNumber);
        break;
      
    }
  }

  bool SolverCore::smartStageOneEnough() {
    size_t stageSize = 0;
    uint8_t i = 0;
    for (auto& it : smartStageOneStorage_) {
      if(it.sender == i) ++stageSize;
      ++i;
    }
    size_t cSize = smartConfidants_.size();
    csdetails() << __func__ << ":         Completed " << stageSize << " of " << cSize;
    return (stageSize == cSize ? true : false);
  }

  bool SolverCore::smartStageTwoEnough() {
    size_t stageSize = 0;
    uint8_t i = 0;
    for (auto& it : smartStageTwoStorage_) {
      if (it.sender == i) ++stageSize;
      ++i;
    }
    size_t cSize = smartConfidants_.size();
    csdetails() << __func__ << ":         Completed " << stageSize << " of " << cSize;
    return (stageSize == cSize ? true : false);
  }

  bool SolverCore::smartStageThreeEnough() {
    size_t stageSize = 0;
    uint8_t i = 0;
    for (auto& it : smartStageThreeStorage_) {
      if (it.sender == i) ++stageSize;
      ++i;
    }
    size_t cSize = smartConfidants_.size() / 2 + 1;
    csdetails() << __func__ << ":         Completed " << stageSize << " of " << cSize;
    return (stageSize == cSize ? true : false);
  }

  void SolverCore::startTimer(int st)
  {
    csdetails() << __func__ << "(): start track timeout " << Consensus::T_stage_request << " ms of stages-" << st << " received";
    timeout_request_stage.start(
      scheduler, Consensus::T_stage_request,
      // timeout #1 handler:
      [this, st]() {
        csdebug() << __func__ << "(): timeout for stages-" << st << " is expired, make requests";
        requestSmartStages(st);
        // start subsequent track timeout for "wide" request
        csdebug() << __func__ << "(): start subsequent track timeout " << Consensus::T_stage_request
          << " ms to request neighbors about stages-" << st;
        timeout_request_neighbors.start(
          scheduler, Consensus::T_stage_request,
          // timeout #2 handler:
          [this, st]() {
            csdebug() << __func__ << "(): timeout for requested stages is expired, make requests to neighbors";
            requestSmartStagesNeighbors(st);
            // timeout #3 handler
            timeout_force_transition.start(
              scheduler, Consensus::T_stage_request,
              [this, st]() {
                csdebug() << __func__ << "(): timeout for transition is expired, mark silent nodes as outbound";
                markSmartOutboundNodes();
              },
            true/*replace if exists*/);
          },
         true /*replace if exists*/);
        },
      true /*replace if exists*/);
  }

  void SolverCore::killTimer(int st) {
    if (timeout_request_stage.cancel()) {
      csdebug() << __func__ << "(): cancel track timeout of stages-" << st;
    }
    if (timeout_request_neighbors.cancel()) {
      csdebug() << __func__ << "(): cancel track timeout to request neighbors about stages-" << st;
    }
    if (timeout_force_transition.cancel()) {
      csdebug() << __func__ << "(): cancel track timeout to force transition to next state";
    }
  }

  void SolverCore::requestSmartStages(int st) {
    csdetails() << __func__;
    uint8_t cnt = (uint8_t) smartConfidants_.size();
    int cnt_requested = 0;
    for (uint8_t i = 0; i < cnt; ++i) {
      switch (st) {
        case 1:
          if (smartStageOneStorage_[i].sender==cs::ConfidantConsts::InvalidConfidantIndex) {
            pnode->stageRequest(MsgTypes::SmartFirstStageRequest, i, i);
          }
          break;
        case 2:
          if (smartStageTwoStorage_[i].sender == cs::ConfidantConsts::InvalidConfidantIndex) {
            pnode->stageRequest(MsgTypes::SmartSecondStageRequest, i, i);
          }
          break;
        case 3:
          if (smartStageThreeStorage_[i].sender == cs::ConfidantConsts::InvalidConfidantIndex) {
            pnode->stageRequest(MsgTypes::SmartThirdStageRequest, i, i);
          }
          break;
      }
        ++cnt_requested;
    }
    if (0 == cnt_requested) {
      csdebug() <<__func__ << ": no node to request";
    }
  }

  // requests stages from any available neighbor nodes
  void SolverCore::requestSmartStagesNeighbors(int st) {
    csdetails() << __func__;
    uint8_t cnt = (uint8_t)smartConfidants_.size();
    int cnt_requested = 0;
    switch (st) {
      case 1:
        for (uint8_t i = 0; i < cnt; ++i) {
          auto required = smartStageOneStorage_[i].sender;
          if (required == cs::ConfidantConsts::InvalidConfidantIndex) {
            if (i != ownSmartsConfNum_ && i!=required) {
                pnode->smartStageRequest(MsgTypes::SmartFirstStageRequest, i , required);
                ++cnt_requested;
            }
          } 
        }
        break;
      case 2:
        for (uint8_t i = 0; i < cnt; ++i) {
          auto required = smartStageTwoStorage_[i].sender;
          if (required == cs::ConfidantConsts::InvalidConfidantIndex) {
            if (i != ownSmartsConfNum_ && i != required) {
              pnode->smartStageRequest(MsgTypes::SmartSecondStageRequest, i, required);
              ++cnt_requested;
            }
          }
        }
        break;
      case 3:
        for (uint8_t i = 0; i < cnt; ++i) {
          auto required = smartStageThreeStorage_[i].sender;
          if (required == cs::ConfidantConsts::InvalidConfidantIndex) {
            if (i != ownSmartsConfNum_ && i != required) {
              pnode->smartStageRequest(MsgTypes::SmartThirdStageRequest, i, required);
              ++cnt_requested;
            }
          }
        }
        break;
        
    }
    if (0 == cnt_requested) {
      csdebug() << __func__ << ": no node to request";
    }
  }

  // forces transition to next stage
  void SolverCore::markSmartOutboundNodes()
  {
    //uint8_t cnt = (uint8_t)context.cnt_trusted();
    //for (uint8_t i = 0; i < cnt; ++i) {
    //  if (context.stage1(i) == nullptr) {
    //    // it is possible to get a transition to other state in SolverCore from any iteration, this is not a problem, simply execute method until end
    //    fake_stage1(i);
    //  }
    //}
  }

  void SolverCore::fakeStage(uint8_t confIndex) {
    csunused(confIndex);
  }

  bool SolverCore::smartConfidantExist(uint8_t confidantIndex) {
    if (confidantIndex < smartConfidants_.size()) {
      return true;
    }
    return false;
  }

  //smart-part end AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA

  uint8_t SolverCore::subRound() {
    return (pnode->subRound());
  }
}  // namespace slv2
