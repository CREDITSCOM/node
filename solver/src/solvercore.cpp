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
    //smartProcesses_.reserve(simultaneuosSmartsNumber_);
    // bind signals
    //cs::Connector::connect(&psmarts->signal_smart_executed, this, &cs::SolverCore::getSmartResult);
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

bool SolverCore::stateFailed(Result res) {
  if (Result::Failure == res) {
    cserror() << "SolverCore: error in state " << (pstate ? pstate->name() : "null");
    return true;
  }
  return false;

}
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
  csmeta(csdetails) << "Timestamp: " << currentTimeStamp;
  for (std::size_t i = 0; i < hashes.size(); ++i) {
    csmeta(csdetails) << '\t' << i << ". " << hashes[i].toString();
  }

  // only for new consensus
  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = pnode->getBlockChain().getLastSequence() + 1;  // change for roundNumber
  poolMetaInfo.timestamp = std::move(currentTimeStamp);

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
  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo);

  if (!pool.has_value()) {
    csmeta(cserror) << "ApplyCharacteristic() failed to create block";
    return;
  }

  uint32_t binSize = 0;
  deferredBlock_ = std::move(pool.value());
  deferredBlock_.set_confidants(conveyer.confidants());

  csmeta(csdebug) << "block #" << deferredBlock_.sequence() << " add new wallets to pool";
  pnode->getBlockChain().addNewWalletsToPool(deferredBlock_);
  pnode->getBlockChain().setTransactionsFees(deferredBlock_);

  deferredBlock_.to_byte_stream(binSize);
  deferredBlock_.hash();

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

bool SolverCore::addSignaturesToDeferredBlock(cs::BlockSignatures&& blockSignatures) {
  csmeta(csdetails) << "begin";
  if (!deferredBlock_.is_valid()) {
    csmeta(cserror) << " ... Failed!!!";
    return false;
  }

  deferredBlock_.set_signatures(std::move(blockSignatures));

  auto resPool = pnode->getBlockChain().createBlock(deferredBlock_);

  if (!resPool.has_value()) {
    csmeta(cserror) << "Blockchain failed to write new block";
    return false;
  }

  deferredBlock_ = csdb::Pool();

  csmeta(csdetails) << "end";
  return true;
}

void SolverCore::removeDeferredBlock() {
  pnode->getBlockChain().removeWalletsInPoolFromCache(deferredBlock_);
  deferredBlock_ = csdb::Pool();
  csdebug() << "SolverCore: just created new block was thrown away";
}

uint8_t SolverCore::subRound() {
  return (pnode->subRound());
}
}  // namespace cs
