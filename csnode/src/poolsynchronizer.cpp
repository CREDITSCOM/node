#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/utils.hpp>

#include <csnode/conveyer.hpp>

#include <net/transport.hpp>

cs::PoolSynchronizer::PoolSynchronizer(const PoolSyncData& data, Transport* transport, BlockChain* blockChain)
: syncData_(data)
, transport_(transport)
, blockChain_(blockChain) {
    neighbours_.reserve(transport_->getMaxNeighbours());

    refreshNeighbours();

    cs::Connector::connect(&timer_.timeOut, this, &cs::PoolSynchronizer::onTimeOut);
    cs::Connector::connect(&roundSimulation_.timeOut, this, &cs::PoolSynchronizer::onRoundSimulation);

    // Print Pool Sync Data Info
    const uint8_t hl = 25;
    const uint8_t vl = 6;
    csmeta(csdebug) << "Pool sync data : \n"
                    << std::setw(hl) << "Fast mode:        " << std::setw(vl) << syncData_->isFastMode << "\n"
                    << std::setw(hl) << "One reply block:  " << std::setw(vl) << syncData_->oneReplyBlock << "\n"
                    << std::setw(hl) << "Block pools:      " << std::setw(vl) << static_cast<int>(syncData_->blockPoolsCount) << "\n"
                    << std::setw(hl) << "Request round:    " << std::setw(vl) << static_cast<int>(syncData_->requestRepeatRoundCount) << "\n"
                    << std::setw(hl) << "Neighbour packets:" << std::setw(vl) << static_cast<int>(syncData_->neighbourPacketsCount) << "\n"
                    << std::setw(hl) << "Polling frequency:" << std::setw(vl) << syncData_->sequencesVerificationFrequency;
}

void cs::PoolSynchronizer::sync(cs::RoundNumber roundNum, cs::RoundNumber difference, bool isBigBand) {
    if (transport_->getNeighboursCount() == 0) {
        csmeta(csdebug) << "Cannot start sync (no neighbours). Needed sequence: " << roundNum << ",   Requested pools block size:" << syncData_->blockPoolsCount;
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

    const bool useTimer = syncData_->sequencesVerificationFrequency > 1;
    const int delay = useTimer ? static_cast<int>(syncData_->sequencesVerificationFrequency) : static_cast<int>(cs::NeighboursRequestDelay);

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

        refreshNeighbours();
        sendBlockRequest();

        if (isBigBand || useTimer) {
            timer_.start(delay, Timer::Type::Standard, RunPolicy::CallQueuePolicy);
        }

        roundSimulation_.start(60000, cs::Timer::Type::HighPrecise, RunPolicy::CallQueuePolicy);  // 1 Min
    }
    else if (syncData_->requestRepeatRoundCount > 0) {
        roundSimulation_.restart();
        const bool isNeedRequest = checkActivity(CounterType::ROUND);
        bool isAvailable = false;

        if (syncData_->sequencesVerificationFrequency == 1) {
            isAvailable = checkActivity(CounterType::TIMER);
        }

		if (isNeedRequest || isAvailable) {
			sendBlockRequest();
		}

		bool nothing_to_request = true;
		for (const auto& neighbour : neighbours_) {
			if (!neighbour.sequences().empty()) {
				nothing_to_request = false;
				break;
			}
		}
		if (nothing_to_request) {
			cslog() << "PoolSyncronizer> No sequence is waited from any neighbour, finish sync";
			synchroFinished();
		}
	}
}

void cs::PoolSynchronizer::syncLastPool() {
    if (isSyncroStarted_) {
        return;
    }

    auto lastWrittenSequence = blockChain_->getLastSeq();
    ConnectionPtr connection;

    {
        auto lock = transport_->getNeighboursLock();
        auto neighbours = transport_->getNeighboursWithoutSS();

        auto iterator = std::find_if(std::begin(neighbours), std::end(neighbours), [lastWrittenSequence](const auto& neighobur) {
            return neighobur->lastSeq > lastWrittenSequence;
        });

        if (iterator != std::end(neighbours)) {
            connection = *iterator;
        }
    }

    if (connection.isNull()) {
        return;
    }

    isSyncroStarted_ = true;
    emit sendRequest(connection, PoolsRequestedSequences { lastWrittenSequence + 1}, 0);
}

void cs::PoolSynchronizer::getBlockReply(cs::PoolsBlock&& poolsBlock, std::size_t packetNum) {
    csmeta(csdebug) << "Get Block Reply <<<<<<< : count: " << poolsBlock.size() << ", seqs: [" << poolsBlock.front().sequence() << ", " << poolsBlock.back().sequence()
                    << "], id: " << packetNum;

    cs::Sequence lastWrittenSequence = blockChain_->getLastSeq();
    const cs::Sequence oldLastWrittenSequence = lastWrittenSequence;
    const std::size_t oldCachedBlocksSize = blockChain_->getCachedBlocksSize();

    // TODO Think, do really need this here?!
    // refreshNeighbours();

    for (auto& pool : poolsBlock) {
        const auto sequence = pool.sequence();

        // sequence will be removed after block appears in chain
        //removeExistingSequence(sequence, SequenceRemovalAccuracy::EXACT);

        if (lastWrittenSequence > sequence) {
            continue;
        }

        if (pool.signatures().size() == 0) {
            cserror() << "PoolSyncronizer> No signatures in pool #" << pool.sequence();
            continue;
        }

        if (blockChain_->storeBlock(pool, true /*by_sync*/)) {
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
            csmeta(csdetails) << "Neighbor: " << static_cast<int>(neighbour.index()) << " is busy";
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
    return syncData_->oneReplyBlock;
}

bool cs::PoolSynchronizer::isFastMode() const {
    if (!isSyncroStarted_ || !syncData_->isFastMode) {
        return false;
    }

    const cs::Sequence sum = cs::Conveyer::instance().currentRoundNumber() - blockChain_->getLastSeq() - blockChain_->getCachedBlocksSize();
    return sum > static_cast<cs::Sequence>(syncData_->blockPoolsCount * 3);  // roundDifferentForSync_
}

//
// Slots
//

void cs::PoolSynchronizer::onTimeOut() {
    if (!isSyncroStarted_) {
        return;
    }

    bool isAvailable = false;

    if (isFastMode()) {
        static uint8_t fastCounter = 0;
        ++fastCounter;
        if (fastCounter > 20) {
            fastCounter = 0;
            csmeta(csdetails) << "OnTimeOut Fast: " << syncData_->sequencesVerificationFrequency * 20;
            isAvailable = checkActivity(cs::PoolSynchronizer::CounterType::ROUND);
        }
    }

    if (!isAvailable) {
        csmeta(csdetails) << "OnTimeOut: " << syncData_->sequencesVerificationFrequency;
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
        neighbours_.front().addSequences(removedSequence);
    }
    else {
        removeExistingSequence(removedSequence, SequenceRemovalAccuracy::EXACT);
    }
}

void cs::PoolSynchronizer::onConfigChanged(const Config& updated) {
    syncData_.exchange(updated.getPoolSyncSettings());
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
    refreshNeighbours();

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
    ConnectionPtr target = getConnection(neighbour);

    if (!target) {
        csmeta(cserror) << "Target is not valid";
        return;
    }

    std::size_t packet = 0;
    const auto& sequences = neighbour.sequences();

    for (const auto& sequence : sequences) {
        if (!requestedSequences_.count(sequence)) {
            requestedSequences_.emplace(std::make_pair(sequence, 0));
        }
        packet = ++(requestedSequences_.at(sequence));
    }

    cslog() << "SYNC: requesting for " << sequences.size() << " blocks [" << sequences.front() << ", " << sequences.back()
        << "] from " << target->getOut() << ", repeat " << packet;

    emit sendRequest(target, sequences, packet);
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
        const int nhIdx = static_cast<int>(neighbour.index());
        for (const auto& [sequence, packet] : requestedSequences_) {
            (void)packet;
            neighbour.addSequences(sequence);
            csmeta(csdetails) << "Is last packet: nh: " << nhIdx << ", add seq: " << sequence;
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
    if (syncData_->neighbourPacketsCount > 0 && !isLastPacket) {
        isNeededHelpIt = std::find_if(requestedSequences_.begin(), requestedSequences_.end(), [this](const auto& pair) { return pair.second >= syncData_->neighbourPacketsCount; });
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

    for (std::size_t i = 0; i < syncData_->blockPoolsCount; ++i) {
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
    if (neighbours_.empty() || neighbours_.front().sequences().empty()) {
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

void cs::PoolSynchronizer::refreshNeighbours() {
    const size_t neededNeighboursCount = transport_->getNeighboursCountWithoutSS();
    const size_t nSize = neighbours_.size();

    if (nSize == neededNeighboursCount) {
        return;
    }

    csmeta(csdetails) << "Neighbours count without ss: " << static_cast<int>(neededNeighboursCount);

    const size_t allNeighboursCount = transport_->getNeighboursCount();

    // 1) sort neighbours by lastSeq descending
    std::multimap<cs::Sequence, size_t, std::greater<cs::Sequence>> sort_by_seq_desc;
    const size_t map_size = transport_->getNeighboursCount();
    for (size_t i = 0; i < map_size; ++i) {
        sort_by_seq_desc.insert(std::make_pair(transport_->getConnectionLastSequence(i), i));
    }

    // Add new neighbours
    if (nSize < neededNeighboursCount) {
        decltype(sort_by_seq_desc)::const_iterator it = sort_by_seq_desc.cbegin();
        // update known neighbors
        for (size_t i = 0; i < nSize; ++i) {
            // get the i-th connection with max sequence
            size_t conn_number = i;
            if (it != sort_by_seq_desc.cend()) {
                // normally, we always be here
                conn_number = it->second;
                ++it;
            }
            auto& item = *(neighbours_.begin() + i);
            ConnectionPtr neighbour = transport_->getConnectionByNumber(conn_number);
            if (neighbour->isSignal) {
                ++conn_number;
                if (conn_number >= neededNeighboursCount) {
                    break;
                }
                neighbour = transport_->getConnectionByNumber(conn_number);
            }
            item.setIndex(uint8_t(conn_number));
            item.setPublicKey(neighbour->key);
        }
        // add new neighbors
        for (size_t i = nSize; i < allNeighboursCount; ++i) {
            // get the i-th connection with max sequence
            size_t conn_number = i;
            if (it != sort_by_seq_desc.cend()) {
                // normally, we always be here
                conn_number = it->second;
                ++it;
            }
            ConnectionPtr neighbour = transport_->getConnectionByNumber(conn_number);
            if (neighbour && !neighbour->isSignal && neighbour->lastSeq) {
                auto isAlreadyHave = std::find_if(neighbours_.begin(), neighbours_.end(), [=](const auto& el) { return size_t(el.index()) == conn_number; });

                if (isAlreadyHave == neighbours_.end()) {
                    neighbours_.emplace_back(NeighboursSetElemet(uint8_t(conn_number), neighbour->key, syncData_->blockPoolsCount));
                }
            }
        }

        csmeta(csdetails) << "Neighbours saved count is: " << neighbours_.size();
        return;
    }

    // refresh neighbours' index
    std::multimap<cs::Sequence, size_t, std::greater<cs::Sequence>>::const_iterator it = sort_by_seq_desc.cbegin();
    std::size_t currentNh = 0;
    for (size_t i = 0; i < allNeighboursCount; ++i) {
        // get the i-th connection with max sequence
        size_t conn_number = i;
        if (it != sort_by_seq_desc.cend()) {
            // normally, we always be here
            conn_number = it->second;
            ++it;
        }
        ConnectionPtr neighbour = transport_->getConnectionByNumber(conn_number);
        if (neighbour && !neighbour->isSignal) {
            neighbours_[currentNh].setIndex(uint8_t(conn_number));
            neighbours_[currentNh].setPublicKey(neighbour->key);
            ++currentNh;
        }
    }

    // remove extra neighbour
    for (size_t i = neededNeighboursCount; i < nSize; ++i) {
        const auto& seqs = neighbours_.back().sequences();
        for (const auto& seq : seqs) {
            bool isAvailable = true;

            if (neighbours_.size() > 1) {
                auto res = std::find_if(neighbours_.begin(), neighbours_.end() - 1,
                                        [seq](const NeighboursSetElemet& el) { return std::find(el.sequences().begin(), el.sequences().end(), seq) != el.sequences().end(); });
                if (res != neighbours_.end() - 1) {
                    isAvailable = false;
                }
            }

            if (isAvailable) {
                requestedSequences_.erase(seq);
            }
        }
        neighbours_.pop_back();
    }

    csmeta(csdetails) << "Neighbours saved count is: " << neighbours_.size();
    printNeighbours("Refresh:");
}

bool cs::PoolSynchronizer::isLastRequest() const {
    const auto sum = cs::Conveyer::instance().currentRoundNumber() - blockChain_->getLastSeq() - blockChain_->getCachedBlocksSize();
    return sum <= syncData_->blockPoolsCount;
}

bool cs::PoolSynchronizer::isAvailableRequest(const cs::PoolSynchronizer::NeighboursSetElemet& nh) const {
    const auto value = nh.roundCounter();

    if (value != 0) {
        return ((value % syncData_->requestRepeatRoundCount) == 0);
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
    neighbours_.clear();

    csmeta(csdebug) << "Synchro finished";
}

ConnectionPtr cs::PoolSynchronizer::getConnection(const NeighboursSetElemet& neighbour) const {
    ConnectionPtr target = transport_->getConnectionByKey(neighbour.publicKey());

    if (!target) {
        target = transport_->getConnectionByNumber(neighbour.index());
    }

    return target;
}

void cs::PoolSynchronizer::printNeighbours(const std::string& funcName) const {
    for (const auto& neighbour : neighbours_) {
        ConnectionPtr target = getConnection(neighbour);

        if (target) {
            csmeta(csdebug) << funcName << " Neighbour: " << target->getOut() << ", " << neighbour;
        }
        else {
            csmeta(csdebug) << funcName << " Neighbour index: " << neighbour.index() << ", does not contained in transport. Neighbours сount: " << transport_->getNeighboursCount()
                            << ", neighbour key: " << cs::Utils::byteStreamToHex(neighbour.publicKey().data(), neighbour.publicKey().size());
        }
    }
}
