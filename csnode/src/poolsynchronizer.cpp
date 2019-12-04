#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/utils.hpp>

#include <csnode/conveyer.hpp>
#include <csnode/configholder.hpp>

#include <net/transport.hpp>

cs::PoolSynchronizer::PoolSynchronizer(Transport* transport, BlockChain* blockChain)
: transport_(transport)
, blockChain_(blockChain) {
    cs::Connector::connect(&timer_.timeOut, this, &cs::PoolSynchronizer::onTimeOut);
    cs::Connector::connect(&roundSimulation_.timeOut, this, &cs::PoolSynchronizer::onRoundSimulation);

    // Print Pool Sync Data Info
    const uint8_t hl = 25;
    const uint8_t vl = 6;
    csmeta(csdebug) << "Pool sync data : \n"
                    << std::setw(hl) << "One reply block:  " << std::setw(vl) << cs::ConfigHolder::instance().config()->getPoolSyncSettings().oneReplyBlock << "\n"
                    << std::setw(hl) << "Block pools:      " << std::setw(vl) << static_cast<int>(cs::ConfigHolder::instance().config()->getPoolSyncSettings().blockPoolsCount) << "\n"
                    << std::setw(hl) << "Request round:    " << std::setw(vl) << static_cast<int>(cs::ConfigHolder::instance().config()->getPoolSyncSettings().requestRepeatRoundCount) << "\n"
                    << std::setw(hl) << "Neighbour packets:" << std::setw(vl) << static_cast<int>(cs::ConfigHolder::instance().config()->getPoolSyncSettings().neighbourPacketsCount) << "\n"
                    << std::setw(hl) << "Polling frequency:" << std::setw(vl) << cs::ConfigHolder::instance().config()->getPoolSyncSettings().sequencesVerificationFrequency;
}

void cs::PoolSynchronizer::sync(cs::RoundNumber roundNum, cs::RoundNumber difference, bool isBigBand) {
    if (transport_->getNeighboursCount() == 0) {
        csmeta(csdebug) << "Cannot start sync (no neighbours). Needed sequence: " << roundNum
                        << ",   Requested pools block size:" << cs::ConfigHolder::instance().config()->getPoolSyncSettings().blockPoolsCount;
        return;
    }

    const cs::Sequence lastWrittenSequence = blockChain_->getLastSeq();

    if (lastWrittenSequence >= roundNum) {
        const bool isFinished = showSyncronizationProgress(lastWrittenSequence);

        if (isFinished) {
            synchroFinished();
        }

        return;
    }

    if (!isSyncroStarted_ && roundNum < (lastWrittenSequence + difference)) {
        return;
    }

    csmeta(csdetails) << "Started";

    if (isSyncroStarted_ && roundNum > 0) {
        --roundNum;
    }

    std::size_t cachedBlocksSize = blockChain_->getCachedBlocksSize();
    cs::Sequence totalBlocks = lastWrittenSequence + cachedBlocksSize;

    if (roundNum < totalBlocks) {
        cswarning() << "Round number is lower than synchro total blocks, do clear cache";
        csdebug() << "SYNC warning, round number " << roundNum << ", total blocks " << totalBlocks;
        blockChain_->clearBlockCache();
        cachedBlocksSize = 0;
        totalBlocks = lastWrittenSequence;
    }

    const cs::Sequence blocksRemaining = roundNum - totalBlocks;

    csdebug() << "SYNC:\n Round num: " << roundNum << "\n Conveyer round: " << cs::Conveyer::instance().currentRoundNumber() << "\n Last written seq: " << lastWrittenSequence
              << "\n Cached blocks: " << cachedBlocksSize << "\n Total blocks: " << totalBlocks;
    cslog() << "SYNC: Blocks remaining: " << blocksRemaining;

    if (blocksRemaining == 0) {
        showSyncronizationProgress(lastWrittenSequence);
        synchroFinished();
        return;
    }

    const bool useTimer = cs::ConfigHolder::instance().config()->getPoolSyncSettings().sequencesVerificationFrequency > 1;
    const int delay = useTimer ? static_cast<int>(cs::ConfigHolder::instance().config()->getPoolSyncSettings().sequencesVerificationFrequency) : static_cast<int>(cs::NeighboursRequestDelay);

    // already synchro start
    if (isSyncroStarted_ && !useTimer) {
        // no BigBang, but no use timer
        if (!isBigBand && timer_.isRunning()) {
            timer_.stop();
        }

        // BigBang received
        if (isBigBand && !timer_.isRunning()) {
            timer_.start(delay, Timer::Type::Standard, RunPolicy::CallQueuePolicy);
        }
    }

    if (!isSyncroStarted_) {
        isSyncroStarted_ = true;

        cs::Connector::connect(&blockChain_->storeBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const csdb::Pool)>(&cs::PoolSynchronizer::onWriteBlock));
        cs::Connector::connect(&blockChain_->cachedBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const cs::Sequence)>(&cs::PoolSynchronizer::onWriteBlock));
        cs::Connector::connect(&blockChain_->removeBlockEvent, this, &cs::PoolSynchronizer::onRemoveBlock);

        sendBlockRequest();

        if (isBigBand || useTimer) {
            timer_.start(delay, Timer::Type::Standard, RunPolicy::CallQueuePolicy);
        }

        roundSimulation_.start(60000, cs::Timer::Type::HighPrecise, RunPolicy::CallQueuePolicy);  // 1 Min
    }
    else if (cs::ConfigHolder::instance().config()->getPoolSyncSettings().requestRepeatRoundCount > 0) {
        roundSimulation_.restart();
        const bool isNeedRequest = checkActivity(CounterType::ROUND);
        bool isAvailable = false;

        if (cs::ConfigHolder::instance().config()->getPoolSyncSettings().sequencesVerificationFrequency == 1) {
            isAvailable = checkActivity(CounterType::TIMER);
        }

        if (isNeedRequest || isAvailable) {
            sendBlockRequest();
        }

        if (std::all_of(std::begin(neighbours_), std::end(neighbours_), [](const auto& neighbour) { return neighbour.sequences().empty(); })) {
            cslog() << "PoolSyncronizer> No sequence is waited from any neighbour, finish sync";
            synchroFinished();
        }
    }
}

void cs::PoolSynchronizer::syncLastPool() {
    if (neighbours_.empty()) {
        return;
    }

    auto lastWrittenSequence = blockChain_->getLastSeq();
    cs::PublicKey target = neighbours_.front().publicKey();

    if (!isSyncroStarted_) {
        isSyncroStarted_ = true;
        cs::Connector::connect(&blockChain_->storeBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const csdb::Pool)>(&cs::PoolSynchronizer::onWriteBlock));
        cs::Connector::connect(&blockChain_->cachedBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const cs::Sequence)>(&cs::PoolSynchronizer::onWriteBlock));
        cs::Connector::connect(&blockChain_->removeBlockEvent, this, &cs::PoolSynchronizer::onRemoveBlock);
    }

    emit sendRequest(target, PoolsRequestedSequences { lastWrittenSequence + 1}, 0);
}

void cs::PoolSynchronizer::getBlockReply(cs::PoolsBlock&& poolsBlock, std::size_t packetNum) {
    csmeta(csdebug) << "Get Block Reply <<<<<<< : count: " << poolsBlock.size() << ", seqs: [" << poolsBlock.front().sequence() << ", " << poolsBlock.back().sequence()
                    << "], id: " << packetNum;

    cs::Sequence lastWrittenSequence = blockChain_->getLastSeq();
    const cs::Sequence oldLastWrittenSequence = lastWrittenSequence;
    const std::size_t oldCachedBlocksSize = blockChain_->getCachedBlocksSize();

    for (auto& pool : poolsBlock) {
        const auto sequence = pool.sequence();

        if (lastWrittenSequence > sequence) {
            continue;
        }

        if (pool.signatures().size() == 0) {
            cserror() << "PoolSyncronizer> No signatures in pool #" << pool.sequence();
            continue;
        }

        //TODO: temp switch off testing confirmations in block received by sync; until fix blocks assembled by init trusted on network restart (issue CP-47)
        if (blockChain_->storeBlock(pool, true /*by_sync*/, true)) {
            blockChain_->testCachedBlocks();
            lastWrittenSequence = blockChain_->getLastSeq();
        }
    }

    if (oldCachedBlocksSize != blockChain_->getCachedBlocksSize() || oldLastWrittenSequence != lastWrittenSequence) {
        const bool isFinished = showSyncronizationProgress(lastWrittenSequence);

        if (isFinished) {
            synchroFinished();
        }
    }
}

void cs::PoolSynchronizer::sendBlockRequest() {
    if (neighbours_.empty()) {
        return;
    }

    csmeta(csdetails) << "Start";

    for (const auto& el : requestedSequences_) {
        csmeta(csdetails) << "Requested sequence: " << el.first << "(" << el.second << ")";
    }

    if (requestedSequences_.empty()) {
        csmeta(csdetails) << "Requested sequence size: 0";
    }

    // remove unnecessary sequences
    removeExistingSequence(blockChain_->getLastSeq(), SequenceRemovalAccuracy::LOWER_BOUND);

    bool success = false;

    for (auto& neighbour : neighbours_) {
        if (!getNeededSequences(neighbour)) {
            csmeta(csdetails) << "Neighbor: " << cs::Utils::byteStreamToHex(neighbour.publicKey()) << " is busy";
            continue;
        }

        if (neighbour.sequences().empty()) {
            csmeta(csdetails) << "All sequences already requested";
            break;
        }

        success = true;
        sendBlock(neighbour);
    }

    if (success) {
        printNeighbours("Info:");
    }
}

bool cs::PoolSynchronizer::isSyncroStarted() const {
    return isSyncroStarted_;
}

bool cs::PoolSynchronizer::isOneBlockReply() const {
    return cs::ConfigHolder::instance().config()->getPoolSyncSettings().oneReplyBlock;
}

//
// Slots
//

void cs::PoolSynchronizer::onTimeOut() {
    if (!isSyncroStarted_) {
        return;
    }

    bool isAvailable = false;

    if (!isAvailable) {
        csmeta(csdetails) << "OnTimeOut: " << cs::ConfigHolder::instance().config()->getPoolSyncSettings().sequencesVerificationFrequency;
        isAvailable = checkActivity(cs::PoolSynchronizer::CounterType::TIMER);
    }

    if (isAvailable) {
        sendBlockRequest();
    }
}

void cs::PoolSynchronizer::onRoundSimulation() {
    csmeta(csdetails) << "on round simulation";

    bool isAvailable = checkActivity(cs::PoolSynchronizer::CounterType::ROUND);

    if (isAvailable) {
        sendBlockRequest();
    }
}

void cs::PoolSynchronizer::onWriteBlock(const csdb::Pool pool) {
    onWriteBlock(pool.sequence());
}

void cs::PoolSynchronizer::onWriteBlock(const cs::Sequence sequence) {
    removeExistingSequence(sequence, SequenceRemovalAccuracy::EXACT);
}

void cs::PoolSynchronizer::onRemoveBlock(const csdb::Pool& pool) {
    cs::Sequence removedSequence = pool.sequence();
    csmeta(csdetails) << removedSequence;
    cs::RoundNumber round = cs::Conveyer::instance().currentRoundNumber();

    if (round > removedSequence && round - removedSequence > cs::PoolSynchronizer::roundDifferentForSync && !neighbours_.empty()) {
        auto iter = std::find_if(std::begin(neighbours_), std::end(neighbours_), [](const auto& neighbour) {
            return neighbour.sequences().empty();
        });

        if (iter == std::end(neighbours_)) {
            iter = neighbours_.begin();
        }

        iter->addSequences(removedSequence);
    }
    else {
        removeExistingSequence(removedSequence, SequenceRemovalAccuracy::EXACT);
    }
}

void cs::PoolSynchronizer::onPingReceived(cs::Sequence sequence, const cs::PublicKey& publicKey) {
    NeighboursSetElemet neighbour(publicKey);

    auto iter = std::find(std::begin(neighbours_), std::end(neighbours_), neighbour);

    if (iter == std::end(neighbours_)) {
        auto lower = std::lower_bound(std::begin(neighbours_), std::end(neighbours_), neighbour, std::greater<NeighboursSetElemet>{});
        iter = neighbours_.insert(lower, neighbour);
    }

    iter->setMaxSequence(sequence);
}

void cs::PoolSynchronizer::onNeighbourAdded(const cs::PublicKey& publicKey, cs::Sequence sequence) {
    NeighboursSetElemet neighbour(publicKey);

    if (auto iter = std::find(std::begin(neighbours_), std::end(neighbours_), neighbour); iter != std::end(neighbours_)) {
        return;
    }

    neighbour.setMaxSequence(sequence);

    auto iter = std::lower_bound(std::begin(neighbours_), std::end(neighbours_), neighbour, std::greater<NeighboursSetElemet>{});
    neighbours_.insert(iter, std::move(neighbour));
}

void cs::PoolSynchronizer::onNeighbourRemoved(const cs::PublicKey& publicKey) {
    NeighboursSetElemet neighbour(publicKey);

    auto iter = std::find(std::begin(neighbours_), std::end(neighbours_), neighbour);

    if (iter == std::end(neighbours_)) {
        return;
    }

    if (!iter->sequences().empty()) {
        auto sequences = iter->sequences();
        auto emptyNeighbourIter = std::find_if(std::begin(neighbours_), std::end(neighbours_), [&](const auto& element) {
            return element.sequences().empty();
        });

        if (emptyNeighbourIter != std::end(neighbours_)) {
            emptyNeighbourIter->setSequences(sequences);
        }
    }

    neighbours_.erase(iter);
}

//
// Service
//

bool cs::PoolSynchronizer::showSyncronizationProgress(const cs::Sequence lastWrittenSequence) const {
    const cs::RoundNumber globalSequence = cs::Conveyer::instance().currentRoundNumber();

    if (!globalSequence) {
        csmeta(cswarning) << "Current round number: 0";
        return false;
    }

    const std::size_t cachedBlocksSize = blockChain_->getCachedBlocksSize();
    const float last = float(lastWrittenSequence + cachedBlocksSize);
    const float global = float(globalSequence - 1);
    if (global < last) {
        return false;
    }
    const float maxValue = 100.0f;
    const uint32_t syncStatus = static_cast<uint32_t>(std::min(((last / global) * maxValue), maxValue));
    const uint32_t remaining = static_cast<uint32_t>(global - last);

    ProgressBar bar;
    std::cout << "\n";
    cslog() << "SYNC: store " << WithDelimiters(lastWrittenSequence)
        << " (+ " << WithDelimiters(cachedBlocksSize)
        << " in cache), remaining " << WithDelimiters(remaining);
    cslog() << "SYNC: " << bar.string(syncStatus) << "\n";

    return remaining == 0;
}

bool cs::PoolSynchronizer::checkActivity(const CounterType counterType) {
    if (neighbours_.empty()) {
        csmeta(csdetails) << "Neighbours count is 0";
        return false;
    }

    csmeta(csdetails) << counterType;
    bool isNeedRequest = false;

    switch (counterType) {
        case CounterType::ROUND:
            for (auto& neighbour : neighbours_) {
                neighbour.increaseRoundCounter();

                if (!isNeedRequest && isAvailableRequest(neighbour)) {
                    isNeedRequest = true;
                }
            }
            csmeta(csdetails) << "isNeedRequest: " << isNeedRequest;
            printNeighbours("Activity Round:");
            break;

        case CounterType::TIMER:
            for (auto& neighbour : neighbours_) {
                isNeedRequest = neighbour.sequences().empty();
                if (isNeedRequest) {
                    break;
                }
            }
            csmeta(csdetails) << "isNeedRequest: " << isNeedRequest;
            break;
    }

    return isNeedRequest;
}

void cs::PoolSynchronizer::sendBlock(const NeighboursSetElemet& neighbour) {
    std::size_t packet = 0;
    const auto& sequences = neighbour.sequences();

    for (const auto& sequence : sequences) {
        if (!requestedSequences_.count(sequence)) {
            requestedSequences_.emplace(std::make_pair(sequence, 0));
        }

        packet = ++(requestedSequences_.at(sequence));
    }

    cslog() << "SYNC: requesting for " << sequences.size() << " blocks [" << sequences.front() << ", " << sequences.back()
        << "] from " << cs::Utils::byteStreamToHex(neighbour.publicKey()) << ", repeat " << packet;

    emit sendRequest(neighbour.publicKey(), sequences, packet);
}

bool cs::PoolSynchronizer::getNeededSequences(NeighboursSetElemet& neighbour) {
    const bool isLastPacket = isLastRequest();
    if (isLastPacket && !requestedSequences_.empty()) {
        csmeta(csdetails) << "Is last packet: requested sequences: [" << requestedSequences_.begin()->first << ", " << requestedSequences_.rbegin()->first << "]";

        const auto& sequences = neighbour.sequences();
        if (!sequences.empty() && requestedSequences_.find(sequences.front()) != requestedSequences_.end()) {
            csmeta(csdetails) << "Is last packet: this neighbour is already requested";
            if (isAvailableRequest(neighbour)) {
                neighbour.resetRoundCounter();
                return true;
            }
            return false;
        }

        neighbour.reset();

        for (const auto& [sequence, packet] : requestedSequences_) {
            (void)packet;
            neighbour.addSequences(sequence);
            csmeta(csdetails) << "Is last packet: add seq: " << sequence;
        }

        csmeta(csdetails) << "Needed sequences size: " << neighbour.sequences().size();
        return true;
    }

    const std::vector<BlockChain::SequenceInterval> requiredBlocks = blockChain_->getRequiredBlocks();

    if (requiredBlocks.empty()) {
        csmeta(csdebug) << "Required blocks is empty !!!";
        return true;
    }

    const cs::Sequence lastWrittenSequence = blockChain_->getLastSeq();

    cs::Sequence sequence = lastWrittenSequence;

    auto isNeededHelpIt = requestedSequences_.end();
    if (cs::ConfigHolder::instance().config()->getPoolSyncSettings().neighbourPacketsCount > 0 && !isLastPacket) {
        isNeededHelpIt = std::find_if(requestedSequences_.begin(), requestedSequences_.end(), [](const auto& pair) {
            return pair.second >= cs::ConfigHolder::instance().config()->getPoolSyncSettings().neighbourPacketsCount;
        });
    }

    // if storage requested sequences is impty
    if (requestedSequences_.empty()) {
        sequence = lastWrittenSequence;
        csmeta(csdetails) << "From blockchain: " << sequence;
    }
    // Needed help another neighbour
    else if (isNeededHelpIt != requestedSequences_.end()) {
        sequence = isNeededHelpIt->first;

        csmeta(csdetails) << "From needed help: " << sequence;

        auto needyNeighbour = std::find_if(neighbours_.begin(), neighbours_.end(),
                                           [sequence](const NeighboursSetElemet& el) { return (!el.sequences().empty() && el.sequences().front() == sequence); });

        if (needyNeighbour == neighbours_.end()) {
            csmeta(cserror) << "Needy neighbour is not valid";
            return false;
        }

        if (neighbour.sequences() == needyNeighbour->sequences()) {
            return true;
        }

        if (!neighbour.sequences().empty() && sequence != neighbour.sequences().front()) {
            for (const auto& seq : neighbour.sequences()) {
                requestedSequences_.erase(seq);
            }
            neighbour.reset();
        }

        neighbour.setSequences(needyNeighbour->sequences());
        return true;
    }
    // Repeat request
    else if (isAvailableRequest(neighbour)) {
        if (!neighbour.sequences().empty()) {
            csmeta(csdetails) << "From repeat request: [" << neighbour.sequences().front() << ", " << neighbour.sequences().back() << "]";
        }

        neighbour.resetRoundCounter();
        return true;
    }
    else {
        sequence = std::max(requestedSequences_.rbegin()->first, lastWrittenSequence);
        csmeta(csdetails) << "From other: " << sequence;
    }

    if (!neighbour.sequences().empty()) {
        return false;
    }

    neighbour.resetSequences();

    for (std::size_t i = 0; i < cs::ConfigHolder::instance().config()->getPoolSyncSettings().blockPoolsCount; ++i) {
        ++sequence;

        // max sequence
        if (requiredBlocks.back().second != 0 && sequence > requiredBlocks.back().second) {
            csmeta(csdetails) << "Max sequence reached";
            break;
        }

        for (std::size_t j = 1; j < requiredBlocks.size(); ++j) {
            // Within a valid pair
            if (sequence > requiredBlocks[j].first && sequence < requiredBlocks[j].second) {
                csmeta(csdetails) << "Check sequence interval: seq: " << sequence << ", Within a valid pair (" << j << "): [" << requiredBlocks[j].first << ", "
                                  << requiredBlocks[j].second << "]";
                break;
            }
            // Between pairs
            if (sequence > requiredBlocks[j - 1].second && sequence < requiredBlocks[j].first) {
                sequence = requiredBlocks[j].first;
                csmeta(csdetails) << "Between pairs: " << sequence;
                break;
            }
        }

        csmeta(csdetails) << "Add sequence for request: " << sequence;
        neighbour.addSequences(sequence);
    }

    return true;
}

void cs::PoolSynchronizer::checkNeighbourSequence(const cs::Sequence sequence, const SequenceRemovalAccuracy accuracy) {
    auto result = std::all_of(std::begin(neighbours_), std::end(neighbours_), [](const auto& neighbour) {
        return neighbour.sequences().empty();
    });

    if (result) {
        return;
    }

    csmeta(csdetails) << sequence;

    bool success = false;

    for (auto& neighbour : neighbours_) {
        success |= neighbour.removeSequnce(sequence, accuracy);

        if (neighbour.sequences().empty()) {
            neighbour.resetRoundCounter();
        }
    }

    if (success) {
        std::sort(neighbours_.begin(), neighbours_.end());
        csmeta(csdetails) << "Remove success sequence: " << sequence;
        printNeighbours("Check seq:");
    }
}

void cs::PoolSynchronizer::removeExistingSequence(const cs::Sequence sequence, const SequenceRemovalAccuracy accuracy) {
    csmeta(csdetails) << "sequence: " << sequence << ", accuracy: " << accuracy;
    checkNeighbourSequence(sequence, accuracy);

    if (!requestedSequences_.empty()) {
        switch (accuracy) {
            case SequenceRemovalAccuracy::EXACT: {
                auto it = requestedSequences_.find(sequence);
                if (it != requestedSequences_.end()) {
                    requestedSequences_.erase(it);
                }
                break;
            }
            case SequenceRemovalAccuracy::LOWER_BOUND:
                requestedSequences_.erase(requestedSequences_.begin(), requestedSequences_.upper_bound(sequence));
                break;
            case SequenceRemovalAccuracy::UPPER_BOUND:
                requestedSequences_.erase(requestedSequences_.lower_bound(sequence), requestedSequences_.end());
                break;
        }
    }
}

bool cs::PoolSynchronizer::isLastRequest() const {
    const auto sum = cs::Conveyer::instance().currentRoundNumber() - blockChain_->getLastSeq() - blockChain_->getCachedBlocksSize();
    return sum <= cs::ConfigHolder::instance().config()->getPoolSyncSettings().blockPoolsCount;
}

bool cs::PoolSynchronizer::isAvailableRequest(const cs::PoolSynchronizer::NeighboursSetElemet& nh) const {
    const auto value = nh.roundCounter();

    if (value != 0) {
        return ((value % cs::ConfigHolder::instance().config()->getPoolSyncSettings().requestRepeatRoundCount) == 0);
    }

    return false;
}

void cs::PoolSynchronizer::synchroFinished() {
    cs::Connector::disconnect(&blockChain_->storeBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const csdb::Pool)>(&cs::PoolSynchronizer::onWriteBlock));
    cs::Connector::disconnect(&blockChain_->cachedBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const cs::Sequence)>(&cs::PoolSynchronizer::onWriteBlock));
    cs::Connector::disconnect(&blockChain_->removeBlockEvent, this, &cs::PoolSynchronizer::onRemoveBlock);

    if (timer_.isRunning()) {
        timer_.stop();
    }

    if (roundSimulation_.isRunning()) {
        roundSimulation_.stop();
    }

    isSyncroStarted_ = false;
    requestedSequences_.clear();

    csmeta(csdebug) << "Synchro finished";
}

void cs::PoolSynchronizer::printNeighbours(const std::string& funcName) const {
    for (const auto& neighbour : neighbours_) {
        if (!neighbour.sequences().empty()) {
            csmeta(csdebug) << funcName << " Neighbour: " << cs::Utils::byteStreamToHex(neighbour.publicKey()) << ", " << neighbour;
        }
    }
}
