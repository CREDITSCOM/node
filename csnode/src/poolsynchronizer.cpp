#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/utils.hpp>

#include <csnode/conveyer.hpp>
#include <csnode/configholder.hpp>

cs::PoolSynchronizer::PoolSynchronizer(BlockChain* blockChain)
: blockChain_(blockChain) {
    cs::Connector::connect(&timer_.timeOut, this, &cs::PoolSynchronizer::onTimeOut);

    // Print Pool Sync Data Info
    const uint8_t hl = 25;
    const uint8_t vl = 6;
    csmeta(csdebug) << "Pool sync data : \n"
                    << std::setw(hl) << "Block pools:      " << std::setw(vl) << static_cast<int>(cs::ConfigHolder::instance().config()->getPoolSyncSettings().blockPoolsCount)
                    << "\n"
                    << std::setw(hl) << "Polling frequency:" << std::setw(vl) << cs::ConfigHolder::instance().config()->getPoolSyncSettings().sequencesVerificationFrequency;
}

void cs::PoolSynchronizer::sync(cs::RoundNumber roundNum, cs::RoundNumber difference) {
    if (neighbours_.empty()) {
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

    if (!isSyncroStarted_) {
        isSyncroStarted_ = true;

        cs::Connector::connect(&blockChain_->storeBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const csdb::Pool&)>(&cs::PoolSynchronizer::onWriteBlock));
        cs::Connector::connect(&blockChain_->cachedBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const cs::Sequence)>(&cs::PoolSynchronizer::onWriteBlock));
        cs::Connector::connect(&blockChain_->removeBlockEvent, this, &cs::PoolSynchronizer::onRemoveBlock);

        timer_.start(cs::ConfigHolder::instance().config()->getPoolSyncSettings().sequencesVerificationFrequency, Timer::Type::HighPrecise, RunPolicy::CallQueuePolicy);

        sendBlockRequest();
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
        cs::Connector::connect(&blockChain_->storeBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const csdb::Pool&)>(&cs::PoolSynchronizer::onWriteBlock));
        cs::Connector::connect(&blockChain_->cachedBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const cs::Sequence)>(&cs::PoolSynchronizer::onWriteBlock));
        cs::Connector::connect(&blockChain_->removeBlockEvent, this, &cs::PoolSynchronizer::onRemoveBlock);
    }

    emit sendRequest(target, PoolsRequestedSequences { lastWrittenSequence + 1});
}

void cs::PoolSynchronizer::getBlockReply(cs::PoolsBlock&& poolsBlock) {
    csmeta(csdebug) << "Get Block Reply <<<<<<< : count: " << poolsBlock.size() << ", seqs: ["
                    << poolsBlock.front().sequence() << ", " << poolsBlock.back().sequence() << "]";

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
        if (blockChain_->storeBlock(pool, cs::PoolStoreType::Synced)) {
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

    for (const auto& el : requestedSequences_) {
        csmeta(csdetails) << "Requested sequence: " << el.first << "(" << el.second << ")";
    }

    if (requestedSequences_.empty()) {
        csmeta(csdetails) << "Requested sequence size: 0";
    }

    // remove unnecessary sequences
    removeExistingSequence(blockChain_->getLastSeq(), SequenceRemovalAccuracy::LowerBound);

    for (auto& neighbour : neighbours_) {
        if (!getNeededSequences(neighbour)) {
            csmeta(csdetails) << "Neighbor: " << cs::Utils::byteStreamToHex(neighbour.publicKey()) << " is busy";
            continue;
        }

        if (neighbour.sequences().empty()) {
            csmeta(csdetails) << "All sequences already requested";
            break;
        }

        sendBlock(neighbour);
    }
}

bool cs::PoolSynchronizer::isSyncroStarted() const {
    return isSyncroStarted_;
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
        isAvailable = checkActivity();
    }

    if (isAvailable) {
        sendBlockRequest();
    }

    auto sequence = blockChain_->getLastSeq();
    auto round = cs::Conveyer::instance().currentRoundNumber();
    auto result = std::all_of(neighbours_.begin(), neighbours_.end(), [](const auto& neighbour) {
        return neighbour.sequences().empty();
    });

    if (sequence < round && result) {
        synchroFinished();
        sync(round);
    }

    if (blockChain_->getCachedBlocksSizeSynced() >= kCachedBlocksLimit) {
        checkCachedBlocks();
    }
}

void cs::PoolSynchronizer::onWriteBlock(const csdb::Pool& pool) {
    onWriteBlock(pool.sequence());
}

void cs::PoolSynchronizer::onWriteBlock(const cs::Sequence sequence) {
    removeExistingSequence(sequence, SequenceRemovalAccuracy::Exact);
}

void cs::PoolSynchronizer::onRemoveBlock(const csdb::Pool& pool) {
    cs::Sequence removedSequence = pool.sequence();
    csmeta(csdetails) << removedSequence;
    cs::RoundNumber round = cs::Conveyer::instance().currentRoundNumber();

    if (round > removedSequence && round - removedSequence > cs::PoolSynchronizer::kRoundDifferentForSync && !neighbours_.empty()) {
        auto iter = std::find_if(std::begin(neighbours_), std::end(neighbours_), [](const auto& neighbour) {
            return neighbour.sequences().empty();
        });

        if (iter == std::end(neighbours_)) {
            iter = neighbours_.begin();
        }

        iter->addSequences(removedSequence);
    }
    else {
        removeExistingSequence(removedSequence, SequenceRemovalAccuracy::Exact);
    }
}

void cs::PoolSynchronizer::onStoreBlockTimeElapsed() {
    if (isSyncroStarted()) {
        synchroFinished();
    }
}

void cs::PoolSynchronizer::onPingReceived(cs::Sequence sequence, const cs::PublicKey& publicKey) {
    Neighbour neighbour(publicKey);

    auto addable = isAddableNeighbour(sequence);
    auto exists = isNeighbourExists(neighbour);

    if (exists && addable) {
        getNeighbour(neighbour).setMaxSequence(sequence);
    }
    else if (exists && !addable) {
        onNeighbourRemoved(publicKey);
    }
    else if (!exists && addable) {
        addNeighbour(neighbour).setMaxSequence(sequence);
    }
}

void cs::PoolSynchronizer::onNeighbourAdded(const cs::PublicKey& publicKey, cs::Sequence sequence) {
    if (!isAddableNeighbour(sequence) || isNeighbourExists(publicKey)) {
        return;
    }

    Neighbour neighbour(publicKey);
    neighbour.setMaxSequence(sequence);

    addNeighbour(neighbour);
}

void cs::PoolSynchronizer::onNeighbourRemoved(const cs::PublicKey& publicKey) {
    Neighbour neighbour(publicKey);

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

    cslog() << "\nSYNC: store " << WithDelimiters(lastWrittenSequence)
        << " (+ " << WithDelimiters(cachedBlocksSize)
        << " in cache), remaining " << WithDelimiters(remaining);
    cslog() << "SYNC: " << bar.string(syncStatus) << "\n";

    return remaining == 0;
}

bool cs::PoolSynchronizer::checkActivity() {
    if (neighbours_.empty()) {
        csmeta(csdetails) << "Neighbours count is 0";
        return false;
    }

    bool isNeedRequest = false;

    for (auto& neighbour : neighbours_) {
        isNeedRequest = neighbour.sequences().empty();

        if (isNeedRequest) {
            break;
        }
    }

    return isNeedRequest;
}

void cs::PoolSynchronizer::sendBlock(const Neighbour& neighbour) {
    const auto& sequences = neighbour.sequences();

    for (const auto& sequence : sequences) {
        if (!requestedSequences_.count(sequence)) {
            requestedSequences_.emplace(std::make_pair(sequence, 0));
        }
    }

    cslog() << "SYNC: requesting for " << sequences.size() << " blocks [" << sequences.front() << ", " << sequences.back()
        << "] from " << cs::Utils::byteStreamToHex(neighbour.publicKey());

    emit sendRequest(neighbour.publicKey(), sequences);
}

void cs::PoolSynchronizer::sendBlock(const cs::PoolSynchronizer::Neighbour& neighbour, const cs::PoolsRequestedSequences& sequences) {
    const auto requestedSize = cs::ConfigHolder::instance().config()->getPoolSyncSettings().blockPoolsCount;
    const auto parts = sequences.size() / requestedSize;
    const auto key = cs::Utils::byteStreamToHex(neighbour.publicKey());

    if (parts <= 1) {
        printFreeBlocks(key, sequences);

        emit sendRequest(neighbour.publicKey(), sequences);
    }
    else {
        const auto seqs = cs::Utils::splitVector(sequences, parts);

        for (const auto& s : seqs) {
            printFreeBlocks(key, s);

            emit sendRequest(neighbour.publicKey(), s);
        }
    }
}

bool cs::PoolSynchronizer::getNeededSequences(Neighbour& neighbour) {
    const bool isLastPacket = isLastRequest();

    if (isLastPacket && !requestedSequences_.empty()) {
        csmeta(csdetails) << "Is last packet: requested sequences: [" << requestedSequences_.begin()->first << ", " << requestedSequences_.rbegin()->first << "]";
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

    // if storage requested sequences is impty
    if (requestedSequences_.empty()) {
        sequence = lastWrittenSequence;
        csmeta(csdetails) << "From blockchain: " << sequence;
    }
    // Needed help another neighbour
    else if (isNeededHelpIt != requestedSequences_.end()) {
        sequence = isNeededHelpIt->first;

        csmeta(csdetails) << "From needed help: " << sequence;

        auto needyNeighbour = std::find_if(neighbours_.begin(), neighbours_.end(), [sequence](const Neighbour& el) {
            return (!el.sequences().empty() && el.sequences().front() == sequence);
        });

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
    }

    if (success) {
        csmeta(csdetails) << "Remove success sequence: " << sequence;
    }
}

void cs::PoolSynchronizer::removeExistingSequence(const cs::Sequence sequence, const SequenceRemovalAccuracy accuracy) {
    csmeta(csdetails) << "sequence: " << sequence << ", accuracy: " << accuracy;
    checkNeighbourSequence(sequence, accuracy);

    if (!requestedSequences_.empty()) {
        switch (accuracy) {
            case SequenceRemovalAccuracy::Exact: {
                auto it = requestedSequences_.find(sequence);
                if (it != requestedSequences_.end()) {
                    requestedSequences_.erase(it);
                }
                break;
            }
            case SequenceRemovalAccuracy::LowerBound:
                requestedSequences_.erase(requestedSequences_.begin(), requestedSequences_.upper_bound(sequence));
                break;
            case SequenceRemovalAccuracy::UpperBound:
                requestedSequences_.erase(requestedSequences_.lower_bound(sequence), requestedSequences_.end());
                break;
        }
    }
}

bool cs::PoolSynchronizer::isLastRequest() const {
    const auto sum = cs::Conveyer::instance().currentRoundNumber() - blockChain_->getLastSeq() - blockChain_->getCachedBlocksSize();
    return sum <= cs::ConfigHolder::instance().config()->getPoolSyncSettings().blockPoolsCount;
}

bool cs::PoolSynchronizer::isAddableNeighbour(cs::Sequence sequence) const {
    auto lastWrittenSequence = blockChain_->getLastSeq();
    return (sequence > lastWrittenSequence) && (sequence >= neighboursMaxSequence());
}

bool cs::PoolSynchronizer::isNeighbourExists(const cs::PublicKey& key) const {
    Neighbour neighbour(key);
    return isNeighbourExists(neighbour);
}

bool cs::PoolSynchronizer::isNeighbourExists(const Neighbour& neighbour) const {
    auto iter = std::find(std::begin(neighbours_), std::end(neighbours_), neighbour);
    return iter != std::end(neighbours_);
}

cs::PoolSynchronizer::Neighbour& cs::PoolSynchronizer::addNeighbour(const cs::PublicKey& key) {
    Neighbour neighbour(key);
    return addNeighbour(neighbour);
}

cs::PoolSynchronizer::Neighbour& cs::PoolSynchronizer::addNeighbour(const Neighbour& neighbour) {
    auto lower = std::lower_bound(std::begin(neighbours_), std::end(neighbours_), neighbour, std::greater<Neighbour>{});
    auto iter = neighbours_.insert(lower, neighbour);
    return *iter;
}

cs::PoolSynchronizer::Neighbour& cs::PoolSynchronizer::getNeighbour(const cs::PublicKey& key) {
    Neighbour neighbour(key);
    return getNeighbour(neighbour);
}

cs::PoolSynchronizer::Neighbour& cs::PoolSynchronizer::getNeighbour(const cs::PoolSynchronizer::Neighbour& neighbour) {
    auto iter = std::find(std::begin(neighbours_), std::end(neighbours_), neighbour);
    return *iter;
}

cs::Sequence cs::PoolSynchronizer::neighboursMaxSequence() const {
    if (neighbours_.empty()) {
        return cs::Sequence{0};
    }

    return neighbours_.front().maxSequence();
}

void cs::PoolSynchronizer::checkCachedBlocks() {
    static BlockChain::SequenceInterval interval{}; // cached interval
    static size_t counter = 0;                      // retry count
    static size_t index = 0;                        // neighbour index
    static const size_t maxCounter = 5;             // max retryies count

    if (!isSyncroStarted_ || !canRequestFreeBlocks) {
        return;
    }

    auto sequences = blockChain_->getFreeSpaceBlocks();

    if (!sequences.has_value() || neighbours_.empty()) {
        return;
    }

    if (interval != sequences.value()) {
        interval = sequences.value();
        counter = 0;
        index = 0;
    }
    else {
        ++counter;

        if (counter > maxCounter) {
            index = nextIndex(index);
            counter = 0;
        }
    }

    auto& neighbour = *(std::next(neighbours_.begin(), static_cast<std::ptrdiff_t>(index)));
    auto [begin, end] = sequences.value();

    PoolsRequestedSequences seqs;
    seqs.reserve(end - begin);

    for (; begin <= end; ++begin) {
        seqs.push_back(begin);
    }

    sendBlock(neighbour, seqs);

    neighbour.addSequences(seqs);
    neighbour.orderSequences();

    canRequestFreeBlocks = false;

    cs::Timer::singleShot(kFreeBlocksTimeoutMs, cs::RunPolicy::ThreadPolicy, [this] {
        canRequestFreeBlocks = true;
    });
}

void cs::PoolSynchronizer::synchroFinished() {
    cs::Connector::disconnect(&blockChain_->storeBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const csdb::Pool&)>(&cs::PoolSynchronizer::onWriteBlock));
    cs::Connector::disconnect(&blockChain_->cachedBlockEvent, this, static_cast<void (PoolSynchronizer::*)(const cs::Sequence)>(&cs::PoolSynchronizer::onWriteBlock));
    cs::Connector::disconnect(&blockChain_->removeBlockEvent, this, &cs::PoolSynchronizer::onRemoveBlock);

    timer_.stop();

    isSyncroStarted_ = false;
    requestedSequences_.clear();

    csmeta(csdebug) << "Synchro finished";
}

size_t cs::PoolSynchronizer::nextIndex(size_t index) const {
    auto count = neighbours_.size();

    if ((index + 1) < count) {
        return index + 1;
    }

    return 0;
}

std::vector<std::pair<cs::PublicKey, cs::Sequence>> cs::PoolSynchronizer::neighbours() const {
    std::vector<std::pair<cs::PublicKey, cs::Sequence>> result;
    result.reserve(neighbours_.size());

    for (const auto& neighbour : neighbours_) {
        result.push_back(std::make_pair(neighbour.publicKey(), neighbour.maxSequence()));
    }

    return result;
}

template<typename T>
void cs::PoolSynchronizer::printFreeBlocks(const T& key, const cs::PoolsRequestedSequences& sequeces) {
    cslog() << "SYNC FREE BLOCKS: requesting for " << sequeces.size() << " blocks [" << sequeces.front() << ", " << sequeces.back()
        << "] from " << key;
}
