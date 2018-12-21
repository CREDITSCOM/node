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
    psmarts = std::make_unique<cs::SmartContracts>(bc);
    // bind signals
    cs::Connector::connect(&psmarts->signal_smart_executed, this, &cs::SolverCore::getSmartResultTransaction);
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
    cslog() << "SolverCore: switch " << (pstate ? pstate->name() : "null") << " -> "
            << (pState ? pState->name() : "null");
  }
  pstate = pState;
  if (!pstate) {
    return;
  }
  pstate->on(*pcontext);

  auto closure = [this]() {
    cslog() << "SolverCore: state " << pstate->name() << " is expired";
    // clear flag to know timeout expired
    tag_state_expired = CallsQueueScheduler::no_tag;
    // control state switch
    std::weak_ptr<INodeState> p1(pstate);
    pstate->expired(*pcontext);
    if (pstate == p1.lock()) {
      // expired state did not change to another one, do it now
      cslog() << "SolverCore: there is no state set on expiration of " << pstate->name();
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
  cslog() << "SolverCore: TRUSTED -> WRITER, do write & send block";

  pnode->becomeWriter();

  cs::RoundTable table;
  table.round = cs::Conveyer::instance().currentRoundNumber() + 1;
  table.confidants = nodes;
  table.hashes = hashes;

  cslog() << "Applying " << hashes.size() << " hashes to ROUND Table";
  for (std::size_t i = 0; i < hashes.size(); ++i) {
    csdetails() << '\t' << i << ". " << hashes[i].toString();
  }

  pnode->prepareMetaForSending(table, currentTimeStamp);
}

  std::vector<cs::PublicKey> SolverCore::smartConfidants() {
    return smartConfidants_;
  }

  void SolverCore::getSmartResultTransaction(const csdb::Transaction& transaction) {
    cs::SmartContractRef smartRef;
    smartConfidants_.clear();
    smartRef.from_user_field(transaction.user_field(trx_uf::new_state::RefStart));
    smartRoundNumber_ = smartRef.sequence;
    pnode->retriveSmartConfidants(smartRoundNumber_ , smartConfidants_);
    ownSmartsConfNum_ = calculateSmartsConfNum();

    cslog() << "WWWWWWWWWWWWWWWWWWWWWWWWWWW  SMART-ROUND: "<< smartRoundNumber_  << " [" << (int)ownSmartsConfNum_ << "] WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
    cslog() << "SMART confidants (" << smartConfidants_.size() << "):";
    refreshSmartStagesStorage();
    if (ownSmartsConfNum_ == 255) {
      return;
    }
    cscrypto::CalculateHash(st1.hash,transaction.to_byte_stream().data(), transaction.to_byte_stream().size());
    currentSmartTransaction_ = transaction;
    st1.sender = ownSmartsConfNum_;
    st1.sRoundNum = smartRoundNumber_;
    addSmartStageOne(st1, true);
  }

  uint8_t SolverCore::calculateSmartsConfNum()
  {
    uint8_t i = 0 ;
    uint8_t ownSmartConfNumber = 255;
    for (auto& e : smartConfidants_) {
      if (e == pnode->getNodeIdKey()) {
        ownSmartConfNumber = i;
      }
      cslog() << "[" << (int)i << "] "
        << (ownSmartConfNumber != 255 && i == ownSmartConfNumber
          ? "me"
          : cs::Utils::byteStreamToHex(e.data(), e.size()));
      ++i;
    }
    if (ownSmartConfNumber == 255) {
      cslog() << "          This NODE is not a confidant one for this smart-contract consensus round";
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
    cslog() << "          " << __func__;
    size_t cSize = smartConfidants_.size();
    smartStageOneStorage_.clear();
    smartStageOneStorage_.resize(cSize);
    smartStageTwoStorage_.clear();
    smartStageTwoStorage_.resize(cSize);
    smartStageThreeStorage_.clear();
    smartStageThreeStorage_.resize(cSize);
    for (int i = 0; i < cSize; ++i) {
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
  }

  void SolverCore::addSmartStageOne( cs::StageOneSmarts& stage, bool send) {
    if (send) {
      pnode->sendSmartStageOne(stage);
    }
    if (smartStageOneStorage_.at(stage.sender).sender == stage.sender) {
      return;
    }
    smartStageOneStorage_.at(stage.sender) = stage;
    for (int i=0; i<smartConfidants_.size(); ++i) {
      cslog() << "[" << i << "] - " << (int)smartStageOneStorage_.at(i).sender;
    }
    cslog() << "          <-- SMART-Stage-1 [" << (int)stage.sender << "]";
    st2.signatures.at(stage.sender) = stage.signature;
    st2.hashes.at(stage.sender) = stage.messageHash;
    if (smartStageOneEnough()) {
      addSmartStageTwo(st2, true);
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
    cslog() << ": <-- SMART-Stage-2 [" << (int)stage.sender << "] = " << smartStageTwoStorage_.size();
    if (smartStageTwoEnough()) {
      processStages();
    }
  }

void SolverCore::processStages() {
  st3.writer = 0;
  addSmartStageThree(st3, true);
}

  void SolverCore::addSmartStageThree(cs::StageThreeSmarts& stage, bool send) {
  cslog() << __func__;
    if (send) {
      cslog() << "____ 1.";
      stage.sender = ownSmartsConfNum_;
      stage.sRoundNum = smartRoundNumber_;
      pnode->sendSmartStageThree(stage);
    }
    if (smartStageThreeStorage_.at(stage.sender).sender == stage.sender) {
      return;
    }
    smartStageThreeStorage_.at(stage.sender) = stage;
    cslog() << ": <-- SMART-Stage-3 [" << (int)stage.sender << "] = " << smartStageThreeStorage_.size();
    if (smartStageThreeEnough()) {
      createFinalTransactionSet();
    }
  }

  void SolverCore::createFinalTransactionSet() {
    cslog() << __func__ << "(): <starting> ownSmartConfNum = " << (int)ownSmartsConfNum_ << ", writer = " << (int)(smartStageThreeStorage_.at(ownSmartsConfNum_).writer);
    if (ownSmartsConfNum_ == smartStageThreeStorage_.at(ownSmartsConfNum_).writer) {
      //currentSmartTransaction_.signature = 
      cs::Conveyer::instance().addTransaction(currentSmartTransaction_);
      cslog() << __func__ << "(): ==============================================> TRANSACTION SENT TO CONVEYER";
      return;
    }
    cslog() << __func__ << "(): ==============================================> someone SENT TRANSACTION TO CONVEYER";
  }

  bool SolverCore::smartStageOneEnough() {
    size_t stageSize = 0;
    uint8_t i = 0;
    for (auto& it : smartStageOneStorage_) {
      if(it.sender == i) ++stageSize;
      ++i;
    }
    size_t cSize = smartConfidants_.size();
    cslog() << __func__ << ":         Completed " << stageSize << " of " << cSize;
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
    cslog() << __func__ << ":         Completed " << stageSize << " of " << cSize;
    return (stageSize == cSize ? true : false);
  }

  bool SolverCore::smartStageThreeEnough() {
    size_t stageSize = 0;
    uint8_t i = 0;
    for (auto& it : smartStageThreeStorage_) {
      if (it.sender == i) ++stageSize;
      ++i;
    }
    size_t cSize = smartConfidants_.size() / 2 +1;
    cslog() << __func__ << ":         Completed " << stageSize << " of " << cSize;
    return (stageSize == cSize ? true : false);
  }


}  // namespace slv2
