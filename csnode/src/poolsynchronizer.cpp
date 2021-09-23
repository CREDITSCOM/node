#include "poolsynchronizer.hpp"

#include <chrono>
#include <random>

#include <lib/system/logger.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/utils.hpp>

#include <csnode/conveyer.hpp>
#include <csnode/configholder.hpp>

#include <solver/consensus.hpp>

namespace {
size_t getRandomIndex(size_t max) {
    std::default_random_engine engine(std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<size_t> distr(0, max);
    return distr(engine);
}
} // namespace

namespace cs {

/// Public methods

PoolSynchronizer::PoolSynchronizer(BlockChain* blockChain)
: blockChain_(blockChain) {
    cs::Connector::connect(
        &timer_.timeOut,
        this,
        &PoolSynchronizer::onTimeOut
    );

    // Print Pool Sync Data Info
    const uint8_t hl = 25;
    const uint8_t vl = 6;
    csmeta(csdebug) << "Pool sync data : \n"
                    << std::setw(hl) << "Block pools:      "
                    << std::setw(vl) << static_cast<int>(cs::ConfigHolder::instance().config()->getPoolSyncSettings().blockPoolsCount) << "\n"
                    << std::setw(hl) << "Polling frequency:"
                    << std::setw(vl) << cs::ConfigHolder::instance().config()->getPoolSyncSettings().sequencesVerificationFrequency;
}

void PoolSynchronizer::sync(cs::RoundNumber roundNum, cs::RoundNumber difference) {
    if (neighbours_.empty()) {
        csdebug() << "SYNC: no actual neighbours to start sync";
        return;
    }
    cs::Sequence lastWrittenSequence = blockChain_->getLastSeq();
    if (lastWrittenSequence >= roundNum && showSyncronizationProgress(lastWrittenSequence)) {
        csdebug() << __func__ << ": call SyncroFinished";
        synchroFinished();
        return;
    }
    if (!isSyncroStarted_ && roundNum < (lastWrittenSequence + difference)) {
        return;
    }
    if (maxRequestedSequence_ != kWrongSequence && maxRequestedSequence_ > lastWrittenSequence) {
        return;
    }

    if (isSyncroStarted_ && roundNum > 0) {
        --roundNum;
    }

    auto cachedBlocksSize = blockChain_->getCachedBlocksSize();
    cs::Sequence totalBlocks = lastWrittenSequence + cachedBlocksSize;

    if (roundNum < totalBlocks) {
        cswarning() << "SYNC: Round number is lower than synchro total blocks, do clear cache";
        csdebug() << "SYNC: warning, round number " << roundNum << ", total blocks " << totalBlocks;

        blockChain_->clearBlockCache();
        cachedBlocksSize = 0;
        totalBlocks = lastWrittenSequence;
    }

    const cs::Sequence blocksRemaining = roundNum - totalBlocks;

    csdebug() << "SYNC:\n Round num: "    << roundNum
              << "\n Conveyer round: "    << cs::Conveyer::instance().currentRoundNumber()
              << "\n Last written seq: "  << lastWrittenSequence
              << "\n Cached blocks: "     << cachedBlocksSize
              << "\n Total blocks: "      << totalBlocks;
    cslog() << "SYNC: Blocks remaining: " << blocksRemaining;

    if (blocksRemaining == 0) {
        showSyncronizationProgress(lastWrittenSequence);
        csdebug() << __func__ << ": call SyncroFinished";
        synchroFinished();
        return;
    }
    csdebug() << "SYNC: before syncro start: " << (isSyncroStarted_ ? "Already started" : "Not yet");

    if (!isSyncroStarted_) {
        isSyncroStarted_ = true;
        timer_.start(
            cs::ConfigHolder::instance().config()->getPoolSyncSettings().sequencesVerificationFrequency,
            Timer::Type::HighPrecise,
            RunPolicy::CallQueuePolicy
        );
        sendBlockRequest();
    }
}

void PoolSynchronizer::syncLastPool() {
    if (neighbours_.empty()) {
        csdebug() << "SYNC: (last pool) no actual neighbours to request the last block";
        return;
    }
    if (!isSyncroStarted_) {
        isSyncroStarted_ = true;
    }
    cs::PublicKey target = std::next(neighbours_.begin(), getRandomIndex(neighbours_.size() - 1))->first;
    emit sendRequest(target, PoolsRequestedSequences{blockChain_->getLastSeq() + 1});
}

void PoolSynchronizer::manageSyncBlocks(cs::PoolsBlock&& poolsBlock) {
    cs::Sequence lastWrittenSequence = blockChain_->getLastSeq();

    const auto oldLastWrittenSequence = lastWrittenSequence;
    const auto oldCachedBlocksSize = blockChain_->getCachedBlocksSize();

    for (auto& pool : poolsBlock) {
        const auto sequence = pool.sequence();
        
        if (lastWrittenSequence > sequence) {
            csdebug() << "skip seq " << sequence;
            continue;
        }

        if (pool.signatures().size() == 0) {
            cserror() << "SYNC: No signatures in pool #" << pool.sequence();
            continue;
        }

        if (blockChain_->storeBlock(pool, cs::PoolStoreType::Synced)) {
            blockChain_->testCachedBlocks();
            lastWrittenSequence = blockChain_->getLastSeq();
        }
    }

    if (oldCachedBlocksSize != blockChain_->getCachedBlocksSize() || oldLastWrittenSequence != lastWrittenSequence) {
        const bool isFinished = showSyncronizationProgress(lastWrittenSequence);

        if (isFinished) {
            csdebug() << __func__ << ": call SyncroFinished";
            synchroFinished();
        }
    }
}

void PoolSynchronizer::getBlockReply(cs::PoolsBlock&& poolsBlock, const cs::PublicKey& sender) {
    csdebug() << "SYNC: Get Block Reply <<<<<<< : count: " << poolsBlock.size()
        << ", seqs: [" << poolsBlock.front().sequence()
        << ", " << poolsBlock.back().sequence() << "]";
    removeSynchroLog(sender);
    //std::thread sThread(&PoolSynchronizer::manageSyncBlocks, this, std::move(poolsBlock));;
    //sThread.detach();
    manageSyncBlocks(std::move(poolsBlock));
}

bool PoolSynchronizer::isSyncroStarted() const {
    return isSyncroStarted_;
}

//
// Slots
//

void PoolSynchronizer::onTimeOut() {
    if (!isSyncroStarted_) {
        return;
    }

    auto currentRound = cs::Conveyer::instance().currentRoundNumber();

    if (blockChain_->getLastSeq() < currentRound) {
        csdebug() << __func__ << ": call SyncroFinished";
        synchroFinished();
        sync(currentRound);
    }
}

void PoolSynchronizer::onStoreBlockTimeElapsed() {
    if (isSyncroStarted()) {
        synchroFinished();
    }
}
//
//void cs::PoolSynchronizer::trySource(cs::Sequence finSeq, cs::PublicKey& source) {
//    Neighbour neighbour(source);
//    auto exists = isNeighbourExists(neighbour);
//    if (exists) {
//        auto maxSeq = getNeighbour(neighbour).maxSequence();
//        if (maxSeq > finSeq) {
//            syncTill(finSeq, source, false);
//        }
//        else {
//            csinfo() << "Max Seq for this source Key will be " << maxSeq;
//            syncTill(maxSeq, source, false);
//        }
//    }
//    else {
//        csinfo() << "There is no neighbour with such key";
//    }
//
//}

//void cs::PoolSynchronizer::showNeighbours() {
//    csinfo() << "Current Neighbours:";
//    for (auto it : neighbours_) {
//        csinfo() << EncodeBase58(it.publicKey().data(), it.publicKey().data() + it.publicKey().size()) << "  " << it.maxSequence();
//    }
//}

//void cs::PoolSynchronizer::syncTill(cs::Sequence finSeq, const cs::PublicKey& source, bool newCall) {
//    csdebug() << __func__;
//    if (!cs::ConfigHolder::instance().config()->isIdleMode()) {
//        csinfo() << "The node is not in IDLE MODE, cant't run such type of syncro";
//    }
//    if (newCall) {
//        requestedSequences_.clear();
//    }
//
//    removeExistingSequence(blockChain_->getLastSeq(), SequenceRemovalAccuracy::LowerBound);
//    auto it = neighbours_.begin();
//    while (it != neighbours_.end()) {
//        if (it->publicKey() == source) {
//            break;
//        }
//        ++it;
//    }
//
//
//    if (it == neighbours_.end()) {
//        csinfo() << "No neighbour with such key: " << EncodeBase58(source.data(), source.data() + source.size());
//        return;
//    }
//    else {
//        if (it->maxSequence() < finSeq) {
//            csinfo() << "Mentioned neighbour doesn't has such sequence: " << finSeq << ", max is " << it->maxSequence();
//            return;
//        }
//        if (blockChain_->getLastSeq() >= finSeq) {
//            targetSequence_ = 0ULL;
//            requestedSequences_.clear();
//            return;
//        }
//        else {
//            targetSequence_ = finSeq;
//        }
//        
//        if (!getNeededSequencesOnly(*it)) {
//            csmeta(csdetails) << "Neighbor: " << EncodeBase58(it->publicKey().data(), it->publicKey().data() + it->publicKey().size()) << " is busy";
//            return;
//        }
//        if (it->maxSequence() >= cs::Conveyer::instance().currentRoundNumber() - MaxRoundDescrepancy) {
//            sendBlock(*it);
//        }
//    }
//
//}

cs::Sequence PoolSynchronizer::getMaxNeighbourSequence() {
    cs::Sequence maxSequence = 0;
    for (auto& it : neighbours_) {
        if (maxSequence < it.second) {
            maxSequence = it.second;
        }
    }
    return maxSequence;
}

void PoolSynchronizer::onPingReceived(cs::Sequence sequence, const cs::PublicKey& publicKey) {
    if (sequence > blockChain_->getLastSeq()) {
        neighbours_[publicKey] = sequence;
    }
    else {
        neighbours_.erase(publicKey);
    }
}

void PoolSynchronizer::onNeighbourRemoved(const cs::PublicKey& publicKey) {
    neighbours_.erase(publicKey);
}

//
// Service
//

bool PoolSynchronizer::showSyncronizationProgress(cs::Sequence lastWrittenSequence) const {
    const cs::RoundNumber globalSequence = cs::Conveyer::instance().currentRoundNumber();

    if (!globalSequence) {
        csmeta(cswarning) << "Current round number: 0";
        return false;
    }

    const size_t cachedBlocksSize = blockChain_->getCachedBlocksSize();
    const float last = float(lastWrittenSequence + cachedBlocksSize);
    const float global = float(globalSequence - 1);

    if (global < last) {
        return false;
    }

    const float maxValue = 100.0f;
    const uint32_t syncStatus = static_cast<uint32_t>(std::min(((last / global) * maxValue), maxValue));
    const uint32_t remaining = static_cast<uint32_t>(global - last);
    const size_t cachedSyncedBlocksSize = blockChain_->getCachedBlocksSizeSynced();
    const size_t cachedCreatedBlocksSize = cachedBlocksSize - cachedSyncedBlocksSize;

    ProgressBar bar;

    cslog() << "\nSYNC: store "      << WithDelimiters(lastWrittenSequence)
            << " (+ "                << WithDelimiters(cachedBlocksSize)
            << " in cache [synced: " << cachedSyncedBlocksSize
            << ", created: "         << cachedCreatedBlocksSize << "]), "
            << "remaining "          << WithDelimiters(remaining);
    cslog() << "SYNC: "              << bar.string(syncStatus) << "\n";

    return remaining == 0;
}

void PoolSynchronizer::sendBlockRequest() {
    csdebug() << __func__;
    if (neighbours_.empty()) {
        csdebug() << "SYNC: no actual neighbours to request required blocks";
        return;
    }

    maxRequestedSequence_ = blockChain_->getLastSeq();

    auto requiredBlocks = blockChain_->getRequiredBlocks();
    auto neighbour = std::next(neighbours_.begin(), getRandomIndex(neighbours_.size() - 1));
    auto end = neighbour;
    updateSynchroLog();
    do {
        auto neededSequences = getNeededSequences(requiredBlocks, neighbour->second);

        if (neededSequences.empty()) {
            csdetails() << "SYNC: All sequences already requested";
            break;
        }
        else {
            maxRequestedSequence_ = neededSequences.back();
        }
        if (checkSynchroLog(neighbour->first)) {
            cslog() << "SYNC: requesting for " << neededSequences.size()
                << " blocks [" << neededSequences.front() << ", " << neededSequences.back()
                << "] from " << cs::Utils::byteStreamToHex(neighbour->first);
            emit sendRequest(neighbour->first, neededSequences);
            addSynchroLog(neighbour->first, neededSequences, SyncroMessage::NoAnswer);
        }

        ++neighbour;
        if (neighbour == neighbours_.end()) {
            neighbour = neighbours_.begin();
        }
    } while (neighbour != end);
}

std::vector<Sequence> PoolSynchronizer::getNeededSequences(
    const std::vector<BlockChain::SequenceInterval>& requiredBlocks,
    Sequence neighbourLastSeq
) {
    if (requiredBlocks.empty()) {
        csdebug() << "SYNC: Required blocks is empty !!!";
        return {};
    }

    Sequence sequence = maxRequestedSequence_;
    std::vector<Sequence> neededSequences;

    for (size_t i = 0; i < cs::ConfigHolder::instance().config()->getPoolSyncSettings().blockPoolsCount; ++i) {
        ++sequence;

        // max sequence
        if (requiredBlocks.back().second != 0 && sequence > requiredBlocks.back().second) {
            break;
        }

        // current neighbour has no more
        if (sequence > neighbourLastSeq) {
            break;
        }

        //the blocks already requested
        auto it = synchroLog_.begin();
        while (it != synchroLog_.end()) {
            auto& tmpSet = std::get<0>(it->second);
            if (std::find(tmpSet.begin(), tmpSet.end(), sequence) != tmpSet.end()) {
                break;
            }
            ++it;
        }

        for (size_t j = 1; j <= requiredBlocks.size(); ++j) {
            // Within a valid pair
            if (sequence >= requiredBlocks[j - 1].first && sequence <= requiredBlocks[j - 1].second) {
                break;
            }
            // Between pairs
            if (sequence > requiredBlocks[j - 1].second && j < requiredBlocks.size() && sequence < requiredBlocks[j].first) {
                sequence = requiredBlocks[j].first;
                break;
            }
        }
        neededSequences.push_back(sequence);
    }
    return neededSequences;
}

void PoolSynchronizer::synchroFinished() {
    timer_.stop();
    isSyncroStarted_ = false;
    maxRequestedSequence_ = kWrongSequence;
    csdebug() << "SYNC: Synchro finished";
}

void PoolSynchronizer::getSyncroMessage(const cs::PublicKey& sender, SyncroMessage msg) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return;
    }
    csdebug() << __func__;
    if (msg == SyncroMessage::AwaitAnswer) {
        changeSynchroLog(sender, msg);
    }
    else {
        removeSynchroLog(sender);
    }
}

void PoolSynchronizer::addSynchroLog(const cs::PublicKey& sender, cs::PoolsRequestedSequences& sequences, SyncroMessage msg) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return;
    }
    //csdebug() << __func__;
    synchroLog_.emplace(sender, std::make_tuple(sequences, msg, cs::Utils::currentTimestamp()));
    //csdebug() << __func__ << " -> " << synchroLog_.size() << ": key " << cs::Utils::byteStreamToHex(sender) << " added as " << static_cast<int>(msg) << " at " << std::get<2>(synchroLog_[sender]);
}

bool PoolSynchronizer::changeSynchroLog(const cs::PublicKey& sender, SyncroMessage msg) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return true;
    }
    //csdebug() << __func__;
    if (synchroLog_.find(sender) == synchroLog_.end()) {
        return false;
    }
    synchroLog_[sender] = std::make_tuple(std::get<0>(synchroLog_[sender]), msg, cs::Utils::currentTimestamp());
    //csdebug() << __func__ << " -> " << synchroLog_.size() << ": key " << cs::Utils::byteStreamToHex(sender) << " changed to " << static_cast<int>(msg) << " at " << std::get<2>(synchroLog_[sender]);
    return true;
}

void PoolSynchronizer::updateSynchroLog() {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return;
    }
    //csdebug() << __func__ << ":";
    auto timeNow = cs::Utils::currentTimestamp();
    auto it = synchroLog_.begin();
    while (it != synchroLog_.end()) {
        auto msg = std::get<1>(it->second);
        auto timeEvent = std::get<2>(it->second);
        //csdebug() << cs::Utils::byteStreamToHex(it->first) << ": " << std::get<0>(it->second).back() << ", time: " << timeEvent << ", status: " << static_cast<int>(msg);
        if ((msg != SyncroMessage::AwaitAnswer && timeNow > timeEvent + 50000) || ((std::get<1>(it->second) == SyncroMessage::NoAnswer && timeNow > timeEvent + 2000))) {
            //csdebug() << __func__ << " -> " << synchroLog_.size() << ": key " << cs::Utils::byteStreamToHex(it->first) << " erased";
            it = synchroLog_.erase(it);
        }
        else {
            ++it;
        }
    }
    //csdebug() << __func__ << ": finished";
}

bool PoolSynchronizer::removeSynchroLog(const cs::PublicKey& sender) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return true;
    }
    //csdebug() << __func__;
    auto it = synchroLog_.find(sender);
    if (it == synchroLog_.end()) {
        return false;
    }
    //csdebug() << __func__ << " -> " << synchroLog_.size() << ": key " << cs::Utils::byteStreamToHex(sender) << " removed";
    it = synchroLog_.erase(it);
    return true;
}

bool PoolSynchronizer::checkSynchroLog(const cs::PublicKey& sender) { //is true when we can request blocks's set
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return true;
    }
    auto it = synchroLog_.find(sender);
    if (synchroLog_.find(sender) != synchroLog_.end()) {
        auto timeEvent = std::get<2>(it->second);
        auto timeNow = cs::Utils::currentTimestamp();
        if (std::get<1>(it->second) == SyncroMessage::AwaitAnswer || (std::get<1>(it->second) == SyncroMessage::NoAnswer && timeNow < timeEvent + 500)) {
            //csdebug() << __func__ << ": key " << cs::Utils::byteStreamToHex(sender) << " is still busy";
            return false;
        }
    }
    //csdebug() << __func__ << ": key " << cs::Utils::byteStreamToHex(sender) << " is free now";
    return true;
}
} // namespace cs
