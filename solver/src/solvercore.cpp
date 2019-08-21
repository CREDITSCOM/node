#include <smartcontracts.hpp>
#include <solvercontext.hpp>
#include <solvercore.hpp>
#include <states/nostate.hpp>

#pragma warning(push)
#pragma warning(disable : 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)

#include <csnode/datastream.hpp>
#include <csnode/walletsstate.hpp>
#include <lib/system/logger.hpp>

#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <list>

namespace
{
    const char* log_prefix = "SolverCore: ";
}

namespace cs {

// initial values for SolverCore options

// To track timeout for active state
constexpr const bool TimeoutsEnabled = false;
// To enable perform a transition to the same state
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

/*, smartProcess_(this)*/ {
    if constexpr (MonitorModeOn) {
        cslog() << log_prefix << "opt_monitor_mode is on, so use special transition table";
        InitMonitorModeTransitions();
    }
    else if constexpr (WebWalletModeOn) {
        cslog() << log_prefix << "opt_web_wallet_mode is on, so use special transition table";
        InitWebWalletModeTransitions();
    }
    else if constexpr (DebugModeOn) {
        cslog() << log_prefix << "opt_debug_mode is on, so use special transition table";
        InitDebugModeTransitions();
    }
    else if constexpr (true) {
        cslog() << log_prefix << "use default transition table";
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
        cswarning() << log_prefix << "cannot start again, already started";
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
        csdetails() << log_prefix << "pstate-off";
        pstate->off(*pcontext);
    }
    if (Consensus::Log) {
        csdebug() << log_prefix << "switch " << (pstate ? pstate->name() : "null") << " -> " << (pState ? pState->name() : "null");
    }
    pstate = pState;
    if (!pstate) {
        return;
    }
    pstate->on(*pcontext);

    auto closure = [this]() {
        csdebug() << log_prefix << "state " << pstate->name() << " is expired";
        // clear flag to know timeout expired
        tag_state_expired = CallsQueueScheduler::no_tag;
        // control state switch
        std::weak_ptr<INodeState> p1(pstate);
        pstate->expired(*pcontext);
        if (pstate == p1.lock()) {
            // expired state did not change to another one, do it now
            csdebug() << log_prefix << "there is no state set on expiration of " << pstate->name();
            // setNormalState();
        }
    };

    // timeout handling
    if (opt_timeouts_enabled) {
        tag_state_expired = scheduler.InsertOnce(Consensus::DefaultStateTimeout, closure, true /*replace if exists*/);
    }
}

void SolverCore::handleTransitions(Event evt) {
    if (!pstate) {
        // unable to work until initTransitions() called
        return;
    }
    if (Event::BigBang == evt) {
        cswarning() << log_prefix << "BigBang on";
    }
    const auto& variants = transitions[pstate];
    if (variants.empty()) {
        cserror() << log_prefix << "there are no transitions for " << pstate->name();
        return;
    }
    auto it = variants.find(evt);
    if (it == variants.cend()) {
        // such event is ignored in current state
        csdebug() << log_prefix << "event " << static_cast<int>(evt) << " ignored in state " << pstate->name();
        return;
    }
    setState(it->second);
}

bool SolverCore::stateCompleted(Result res) {
    if (Result::Failure == res) {
        cserror() << log_prefix << "error in state " << (pstate ? pstate->name() : "null");
    }
    return (Result::Finish == res);
}

bool SolverCore::stateFailed(Result res) {
    if (Result::Failure == res) {
        cserror() << log_prefix << "error in state " << (pstate ? pstate->name() : "null");
        return true;
    }
    return false;
}

// void SolverCore::adjustTrustedCandidates(cs::Bytes mask, cs::PublicKeys& confidants) {
//  for (int i = 0; i < mask.size(); ++i) {
//    if (mask[i] == cs::ConfidantConsts::InvalidConfidantIndex) {
//      auto it = std::find(trusted_candidates.cbegin(), trusted_candidates.cend(), confidants[i]);
//      if (it != trusted_candidates.cend()) {
//        trusted_candidates.erase(it);
//      }
//    }
//  }
//}

uint64_t SolverCore::lastTimeStamp() {
    int64_t lts;
    try {
        lts = std::stoll(pnode->getBlockChain().getLastTimeStamp());
    }
    catch (...) {
        csdebug() << "Timestamp was announced as zero";
        return 0;
    }
    return lts;
}

std::string SolverCore::chooseTimeStamp(cs::Bytes mask) {
    int64_t lastTimeStamp;
    try {
      lastTimeStamp = std::stoll(pnode->getBlockChain().getLastTimeStamp());
    } catch(...) {
        csdebug() << "ChooseTimeStamp - Timestamp was announced as zero";
      return std::to_string(0);
    }

    std::list<double> stamps;
    double sx = 0, sx2 = 0;
    double mean = 0.0;
    int N = 0;
    for (auto& it : stageOneStorage) {
        if (it.sender >= cs::Conveyer::instance().confidantsCount()) {
            continue;
        }
        if (!it.roundTimeStamp.empty() && mask[it.sender] != cs::ConfidantConsts::InvalidConfidantIndex) {
            int64_t tStamp;
            try {
                tStamp = std::stoll(it.roundTimeStamp);
            } catch(...) {
                cswarning() << log_prefix << "incompatible timestamp received from [" << (int)it.sender << "]";
                continue;
            }
            double x = static_cast<double>(tStamp - lastTimeStamp);
            if (x > DBL_EPSILON) {
                stamps.push_back(x);
                sx += x;
                sx2 += x * x;
                ++N;
            }
        }
    }

    bool isDrop = true;
    while (isDrop) {
        if (N == 0) {
            csdebug() << "There is no nodes with valid TimeStamp";
            int N0 = 0;
            int sx0 = 0;
            for (auto& it : stageOneStorage) {
                int64_t tStamp;
                try {
                    tStamp = std::stoll(it.roundTimeStamp);
                }
                catch (...) {
                    cswarning() << log_prefix << "incompatible timestamp received from [" << (int)it.sender << "]";
                    continue;
                }
                ++N0;
                sx0 += tStamp;
            }

            if (N0 > 0) {
                return std::to_string(sx0/N0);
            }
            else {
                return std::to_string(lastTimeStamp + 100);
            }

        }
        double N_1 = 1. / N;
        mean = sx * N_1;
        double disp = sx2 * N_1 - mean * mean;
        isDrop = false;
        for (auto it = stamps.begin(), end = stamps.end(); it != end;) {
            double x = *it;
            auto it_ = it++;
            double value = x - mean;
            if (value * value >  9. * disp) {
                sx -= x; sx2 -= x * x; --N;
                stamps.erase(it_);
                isDrop = true;
            }
        }
    }

    auto meanTimeStamp = static_cast<int64_t>(lastTimeStamp + mean + 0.5);
    csdebug() << "finish: " << meanTimeStamp;
    return std::to_string(meanTimeStamp);
}

// TODO: this function is to be implemented the block and RoundTable building <====
void SolverCore::spawn_next_round(const cs::PublicKeys& nodes, const cs::PacketsHashes& hashes, std::string&& currentTimeStamp, cs::StageThree& stage3) {
    csmeta(csdetails) << "start";
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    if (conveyer.roundTable(conveyer.currentRoundNumber()) == nullptr) {
        // test if is full round data available (metaStarage[r] + roundtable in metastorage[r])
        return;
    }
    cs::RoundTable table;
    //TODO: place here adjustTrustedCandidates() call
    table.round = conveyer.currentRoundNumber() + 1;
    table.confidants = nodes;
    table.hashes = hashes;
    justCreatedRoundPackage.updateRoundTable(table);
    csdetails() << log_prefix << "applying " << hashes.size() << " hashes to ROUND Table";

    // only for new consensus
    cs::PoolMetaInfo poolMetaInfo;
    std::string timeStamp = conveyer.currentRoundNumber() == 1 ? currentTimeStamp : chooseTimeStamp(stage3.realTrustedMask);
    poolMetaInfo.sequenceNumber = pnode->getBlockChain().getLastSeq() + 1;  // change for roundNumber
    poolMetaInfo.timestamp = std::move(timeStamp);
    auto ptr = conveyer.characteristic(conveyer.currentRoundNumber());
    if (ptr != nullptr) {
        poolMetaInfo.characteristic.mask = ptr->mask;
    }
    //const auto confirmation = pnode->getConfirmation(conveyer.currentRoundNumber());
    //if (confirmation.has_value()) {
    //    poolMetaInfo.confirmationMask = confirmation.value().mask;
    //    poolMetaInfo.confirmations = confirmation.value().signatures;
    //}

    csdebug() << log_prefix << "timestamp: " << poolMetaInfo.timestamp;
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        csdetails() << log_prefix << '\t' << i << ". " << hashes[i].toString();
    }

    //if (stage3.sender != cs::ConfidantConsts::InvalidConfidantIndex) {
    //    const cs::ConfidantsKeys& confidants = conveyer.confidants();
    //    if (stage3.writer < confidants.size()) {
    //        poolMetaInfo.writerKey = confidants[stage3.writer];
    //    }
    //    else {
    //        cserror() << log_prefix << "stage-3 writer index: " << static_cast<int>(stage3.writer)
    //            << ", out of range is current confidants size: " << confidants.size();
    //    }
    //}

    poolMetaInfo.realTrustedMask = stage3.realTrustedMask;
    poolMetaInfo.previousHash = pnode->getBlockChain().getLastHash();

    justCreatedRoundPackage.updatePoolMeta(poolMetaInfo);
    cs::Bytes confirmationMask;
    cs::Signatures confirmations;
    // TODO: in this method we delete the local hashes - so if we need to rebuild thid pool again from the roundTable it's impossible
    uint32_t binSize = 0;
    if (stage3.iteration == 0) {
        std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo);

        if (!pool.has_value()) {
            cserror() << log_prefix << "applyCharacteristic() failed to create block";
            return;
        }
//        uploadNewStates(conveyer.uploadNewStates());
        deferredBlock_ = std::move(pool.value());
        deferredBlock_.set_confidants(conveyer.confidants());

        csmeta(csdebug) << "block #" << deferredBlock_.sequence() << " add new wallets to pool";
        pnode->getBlockChain().addNewWalletsToPool(deferredBlock_);
        pnode->getBlockChain().setTransactionsFees(deferredBlock_);
        const auto confirmation = pnode->getConfirmation(conveyer.currentRoundNumber());
        if (confirmation.has_value()) {
            confirmationMask = confirmation.value().mask;
            confirmations = confirmation.value().signatures;
        }
        if (justCreatedRoundPackage.poolMetaInfo().sequenceNumber > 1) {
            deferredBlock_.add_number_confirmations(static_cast<uint8_t>(confirmationMask.size()));
            deferredBlock_.add_confirmation_mask(cs::Utils::maskToBits(confirmationMask));
            deferredBlock_.add_round_confirmations(confirmations);
        }
    }
    else {
        csdb::Pool tmpPool;
        tmpPool.set_sequence(deferredBlock_.sequence());
        tmpPool.set_previous_hash(deferredBlock_.previous_hash());
        tmpPool.add_real_trusted(cs::Utils::maskToBits(stage3.realTrustedMask));
        tmpPool.add_number_trusted(static_cast<uint8_t>(stage3.realTrustedMask.size()));
        tmpPool.setRoundCost(deferredBlock_.roundCost());
        tmpPool.set_confidants(deferredBlock_.confidants());
        for (auto& it : deferredBlock_.transactions()) {
            tmpPool.add_transaction(it);
        }
        tmpPool.add_user_field(0, justCreatedRoundPackage.poolMetaInfo().timestamp);
        for (auto& it : deferredBlock_.smartSignatures()) {
            tmpPool.add_smart_signature(it);
        }

        csdb::Pool::NewWallets* newWallets = tmpPool.newWallets();
        csdb::Pool::NewWallets* defWallets = deferredBlock_.newWallets();
        if (!newWallets) {
            cserror() << log_prefix << "newPool is read-only";
            return;
        }
        for (auto& it : *defWallets) {
            newWallets->push_back(it);
        }
        const auto confirmation = pnode->getConfirmation(conveyer.currentRoundNumber());
        if (confirmation.has_value()) {
            confirmationMask = confirmation.value().mask;
            confirmations = confirmation.value().signatures;
        }
        if (poolMetaInfo.sequenceNumber > 1) {
            tmpPool.add_number_confirmations(static_cast<uint8_t>(confirmationMask.size()));
            tmpPool.add_confirmation_mask(cs::Utils::maskToBits(confirmationMask));
            tmpPool.add_round_confirmations(confirmations);
        }

        deferredBlock_ = csdb::Pool{};
        deferredBlock_ = tmpPool;
    }
    deferredBlock_.to_byte_stream(binSize);
    deferredBlock_.hash();
    csdetails() << log_prefix << "pool #" << deferredBlock_.sequence() << ": " << cs::Utils::byteStreamToHex(deferredBlock_.to_binary().data(), deferredBlock_.to_binary().size());
    const auto lastHashBin = deferredBlock_.hash().to_binary();
    std::copy(lastHashBin.cbegin(), lastHashBin.cend(), stage3.blockHash.begin());
    stage3.blockSignature = cscrypto::generateSignature(private_key, stage3.blockHash.data(), stage3.blockHash.size());

    //pnode->prepareRoundTable(table, poolMetaInfo, stage3);
    //csmeta(csdetails) << "end";

    const cs::Characteristic* block_characteristic = conveyer.characteristic(conveyer.currentRoundNumber());

    if (!block_characteristic) {
        csmeta(cserror) << "Send round info characteristic not found, logic error";
        return;
    }

    cs::Bytes bytes = justCreatedRoundPackage.bytesToSign();
    stage3.roundHash = cscrypto::calculateHash(bytes.data(), bytes.size());

    cs::Bytes messageToSign;
    messageToSign.reserve(sizeof(cs::RoundNumber) + sizeof(uint8_t) + sizeof(cs::Hash));
    cs::DataStream signStream(messageToSign);
    signStream << justCreatedRoundPackage.roundTable().round;
    signStream << pnode->subRound();
    signStream << stage3.roundHash;
    stage3.roundSignature = cscrypto::generateSignature(private_key, stage3.roundHash.data(), stage3.roundHash.size());

    cs::Bytes trustedList;
    cs::DataStream tStream(trustedList);
    tStream << justCreatedRoundPackage.roundTable().round;
    tStream << justCreatedRoundPackage.roundTable().confidants;
    stage3.trustedHash = cscrypto::calculateHash(trustedList.data(), trustedList.size());
    stage3.trustedSignature = cscrypto::generateSignature(private_key, stage3.trustedHash.data(), stage3.trustedHash.size());

    csdebug() << "NODE> StageThree prepared:" << std::endl << cs::StageThree::toString(stage3);
}

void SolverCore::uploadNewStates(std::vector<csdb::Transaction> newStates) {
    //psmarts.
}

void SolverCore::sendRoundTable() {
    pnode->sendRoundTable(justCreatedRoundPackage);
}

bool SolverCore::addSignaturesToDeferredBlock(cs::Signatures&& blockSignatures) {
    csmeta(csdetails) << "begin";
    if (!deferredBlock_.is_valid()) {
        csmeta(cserror) << " ... Failed!!!";
        return false;
    }

    for (auto& it : blockSignatures) {
        csdetails() << log_prefix << cs::Utils::byteStreamToHex(it.data(), it.size());
    }
    deferredBlock_.set_signatures(blockSignatures);

    auto resPool = pnode->getBlockChain().createBlock(deferredBlock_);

    if (!resPool.has_value()) {
        cserror() << log_prefix << "Blockchain failed to write new block";
        return false;
    }
    //pnode->cleanConfirmationList(deferredBlock_.sequence());
    deferredBlock_ = csdb::Pool();

    csmeta(csdetails) << "end";
    updateLastPackageSignatures();
    return true;
}

void SolverCore::updateLastPackageSignatures() {

    justCreatedRoundPackage.updatePoolSignatures(lastSentSignatures_.poolSignatures);
    justCreatedRoundPackage.updateRoundSignatures(lastSentSignatures_.roundSignatures);
    justCreatedRoundPackage.updateTrustedSignatures(lastSentSignatures_.trustedConfirmation);

    //auto ptr = pnode->getCurrentRoundPackage();
    //if (ptr != nullptr) {
    //    if (justCreatedRoundPackage.roundTable().round == ptr->roundTable().round) {
    //        if (cs::TrustedMask::trustedSize(justCreatedRoundPackage.poolMetaInfo().realTrustedMask)
    //            <= cs::TrustedMask::trustedSize(ptr->poolMetaInfo().realTrustedMask)) {
    //            return;
    //        }

    //    }
    //}

    //pnode->addRoundPackageToList(justCreatedRoundPackage);

    //if (!pnode->addRPackageToCache(justCreatedRoundPackage)) {
    //    return;
    //}
}

void SolverCore::removeDeferredBlock(cs::Sequence seq) {
    if (deferredBlock_.sequence() == seq) {
        pnode->getBlockChain().removeWalletsInPoolFromCache(deferredBlock_);
        deferredBlock_ = csdb::Pool();
        csdebug() << log_prefix << "just created new block was thrown away";
    }
    else {
        csdebug() << log_prefix << "we don't have the correct block to throw";
    }
}

uint8_t SolverCore::subRound() {
    return (pnode->subRound());
}

}  // namespace cs
