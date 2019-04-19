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
#include <csnode/datastream.hpp>
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
, pnode(nullptr)
, pws(nullptr)
, psmarts(nullptr)

/*, smartProcess_(this)*/{
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
    csdebug() << "SolverCore: pstate-off";
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

bool SolverCore::stateFailed(Result res) {
  if (Result::Failure == res) {
    cserror() << "SolverCore: error in state " << (pstate ? pstate->name() : "null");
    return true;
  }
  return false;

}

std::string SolverCore::chooseTimeStamp() {
  csdebug() << "start";
  std::vector<long long int> stamps;
  long long int lastTimeStamp = std::atoll(pnode->getBlockChain().getLastTimeStamp().c_str());
  long long int tmp = 0;
  long long int average = 0;
  int sizeAve = 0;
  for (auto& it : stageOneStorage) {
    if (it.roundTimeStamp != "") {
      tmp = std::atoll(it.roundTimeStamp.c_str());
      csdebug() << "tmp = " << std::to_string(tmp) << " last time stamp = " << std::to_string(lastTimeStamp);
      if (tmp > lastTimeStamp) {
        stamps.push_back(tmp);
        average += tmp;
        ++sizeAve;
      }
    }
  }
  csdebug() << "start1";
  if (sizeAve != 0) {
    average = average / sizeAve;
    csdebug() << "average = " << std::to_string(average);
  }
  else {
    csdebug() << "denominator = 0";
  }

  int sizeAveNew = 0;
  sizeAve = 0;
  long long curAve;
  long double sigma;
  long long int sigmaInt;
  long long int disp;
  while (true) {
    csdebug() << "start2";
    disp = 0;
    for (auto it : stamps) {
      if (it != 0) {
        disp += (it - average)*(it - average);
      }
    }
    disp /= stamps.size();
    curAve = average;
    sigma = std::sqrtl(static_cast<long double>(disp));
    sigmaInt = static_cast<long long int>(sigma);
    csdebug() << "sigma = " << std::to_string(sigmaInt);
    sizeAve = 0;
    sizeAveNew = 0;
    average = 0;
    for (int i = 0; i < stamps.size(); ++i) {
      if (stamps[i] != 0) {
        if (stamps[i] > curAve + 3 * sigmaInt) {
          stamps[i] = 0;
        }
        else {
          average += stamps[i];
          ++sizeAveNew;
        }
        ++sizeAve;
      }
    }
    average /= sizeAveNew;
    if (sizeAve == sizeAveNew) {
      break;
    }
  }

  csdebug() << "finish: " << std::to_string(average);
  return std::to_string(average);
}

//void SolverCore::adjustTrustedCandidates(cs::Bytes mask, cs::PublicKeys& confidants) {
//  for (int i = 0; i < mask.size(); ++i) {
//    if (mask[i] == cs::ConfidantConsts::InvalidConfidantIndex) {
//      auto it = std::find(trusted_candidates.cbegin(), trusted_candidates.cend(), confidants[i]);
//      if (it != trusted_candidates.cend()) {
//        trusted_candidates.erase(it);
//      }
//    }
//  }
//}


//TODO: this function is to be implemented the block and RoundTable building <====
void SolverCore::spawn_next_round(const cs::PublicKeys& nodes,
                                  const cs::PacketsHashes& hashes,
                                  std::string&& currentTimeStamp,
                                  cs::StageThree& stage3) {
  csmeta(csdetails) << "start";
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  cs::RoundTable table;
  table.round = conveyer.currentRoundNumber() + 1;
  table.confidants = nodes;
  table.hashes = hashes;

  csmeta(csdetails) << "Applying " << hashes.size() << " hashes to ROUND Table";

  // only for new consensus
  cs::PoolMetaInfo poolMetaInfo;
  std::string timeStamp = chooseTimeStamp();
  poolMetaInfo.sequenceNumber = pnode->getBlockChain().getLastSequence() + 1;  // change for roundNumber
  poolMetaInfo.timestamp = std::move(timeStamp);
  
  const auto confirmation = pnode->getConfirmation(conveyer.currentRoundNumber());
  if (confirmation.has_value()) {
    poolMetaInfo.confirmationMask = confirmation.value().mask;
    poolMetaInfo.confirmations = confirmation.value().signatures;
  }

   csmeta(csdetails) << "Timestamp: " << poolMetaInfo.timestamp;
  for (std::size_t i = 0; i < hashes.size(); ++i) {
    csmeta(csdetails) << '\t' << i << ". " << hashes[i].toString();
  }
   
  if (stage3.sender != cs::ConfidantConsts::InvalidConfidantIndex) {
    const cs::ConfidantsKeys& confidants = conveyer.confidants();
    if (stage3.writer < confidants.size()) {
      poolMetaInfo.writerKey = confidants[stage3.writer];
    }
    else {
      csmeta(cserror) << "stage-3 writer index: " << static_cast<int>(stage3.writer)
                      << ", out of range is current confidants size: " << confidants.size();
    }
  }

  poolMetaInfo.realTrustedMask = stage3.realTrustedMask;
  poolMetaInfo.previousHash = pnode->getBlockChain().getLastHash();
  
  //TODO: in this method we delete the local hashes - so if we need to rebuild thid pool again from the roundTable it's impossible
  uint32_t binSize = 0;
  if(stage3.iteration == 0){
    std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo);

    if (!pool.has_value()) {
      csmeta(cserror) << "ApplyCharacteristic() failed to create block";
      return;
    }


    deferredBlock_ = std::move(pool.value());
    deferredBlock_.set_confidants(conveyer.confidants());

    csmeta(csdebug) << "block #" << deferredBlock_.sequence() << " add new wallets to pool";
    pnode->getBlockChain().addNewWalletsToPool(deferredBlock_);
    pnode->getBlockChain().setTransactionsFees(deferredBlock_);
  }
  else {
    csdb::Pool tmpPool;
    tmpPool.set_sequence(deferredBlock_.sequence());
    tmpPool.set_previous_hash(deferredBlock_.previous_hash());
    tmpPool.add_real_trusted(cs::Utils::maskToBits(stage3.realTrustedMask));
    tmpPool.add_number_trusted(static_cast<uint8_t>(stage3.realTrustedMask.size()));
    tmpPool.setRoundCost(deferredBlock_.roundCost());
    tmpPool.set_confidants(deferredBlock_.confidants());
    for(auto& it: deferredBlock_.transactions()) {
      tmpPool.add_transaction(it);
    }
    tmpPool.add_user_field(0, poolMetaInfo.timestamp);
    for (auto& it : deferredBlock_.smartSignatures()) {
      tmpPool.add_smart_signature(it);
    }

    csdb::Pool::NewWallets* newWallets = tmpPool.newWallets();
    csdb::Pool::NewWallets* defWallets = deferredBlock_.newWallets();
    if (!newWallets) {
      cserror() << "newPool is read-only";
      return;
    }
    for (auto& it : *defWallets) {
      newWallets->push_back(it);
    }
    if (poolMetaInfo.sequenceNumber > 1) {
      tmpPool.add_number_confirmations(static_cast<uint8_t>(poolMetaInfo.confirmationMask.size()));
      tmpPool.add_confirmation_mask(cs::Utils::maskToBits(poolMetaInfo.confirmationMask));
      tmpPool.add_round_confirmations(poolMetaInfo.confirmations);
    }

    deferredBlock_ = csdb::Pool{};
    deferredBlock_ = tmpPool;
  }
  deferredBlock_.to_byte_stream(binSize);
  deferredBlock_.hash();
  csdebug() << "Pool #" << deferredBlock_.sequence() << ": " << cs::Utils::byteStreamToHex(deferredBlock_.to_binary().data(), deferredBlock_.to_binary().size());
  const auto lastHashBin = deferredBlock_.hash().to_binary();
  std::copy(lastHashBin.cbegin(), lastHashBin.cend(), stage3.blockHash.begin());
  stage3.blockSignature = cscrypto::generateSignature(private_key,
                                                      stage3.blockHash.data(),
                                                      stage3.blockHash.size());

  pnode->prepareRoundTable(table, poolMetaInfo, stage3);
  csmeta(csdetails) << "end";
}

void SolverCore::sendRoundTable() {
  pnode->sendRoundTable();
}

bool SolverCore::addSignaturesToDeferredBlock(cs::Signatures&& blockSignatures) {
  csmeta(csdetails) << "begin";
  if (!deferredBlock_.is_valid()) {
    csmeta(cserror) << " ... Failed!!!";
    return false;
  }

  for (auto& it : blockSignatures) {
    csdebug() << cs::Utils::byteStreamToHex(it.data(), it.size());
  }
  deferredBlock_.set_signatures(blockSignatures);

  auto resPool = pnode->getBlockChain().createBlock(deferredBlock_);

  if (!resPool.has_value()) {
    csmeta(cserror) << "Blockchain failed to write new block";
    return false;
  }
  pnode->cleanConfirmationList(deferredBlock_.sequence());
  deferredBlock_ = csdb::Pool();

  csmeta(csdetails) << "end";
  return true;
}

void SolverCore::removeDeferredBlock(cs::Sequence seq)  {
  if(deferredBlock_.sequence() == seq) {
  pnode->getBlockChain().removeWalletsInPoolFromCache(deferredBlock_);
  deferredBlock_ = csdb::Pool();
  csdebug() << "SolverCore: just created new block was thrown away";
  }
  else {
    csdebug() << "SolverCore: we don't have the correct block to throw";
  }
}

uint8_t SolverCore::subRound() {
  return (pnode->subRound());
}

bool SolverCore::isContractLocked(const csdb::Address& address) const {
  return psmarts->is_contract_locked(address);
}
}  // namespace cs
