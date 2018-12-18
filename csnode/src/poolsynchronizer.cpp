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

  // Print Pool Sync Data Info
  const uint8_t hl = 25;
  const uint8_t vl = 6;
  cslog() << "POOL SYNCHRONIZER> Pool sync data : \n"
          << std::setw(hl) << "One reply block:  " << std::setw(vl) << syncData_.oneReplyBlock << "\n"
          << std::setw(hl) << "Block pools:      " << std::setw(vl) << cs::numeric_cast<int>(syncData_.blockPoolsCount)
          << "\n"
          << std::setw(hl) << "Request round:    " << std::setw(vl)
          << cs::numeric_cast<int>(syncData_.requestRepeatRoundCount) << "\n"
          << std::setw(hl) << "Neighbour packets:" << std::setw(vl)
          << cs::numeric_cast<int>(syncData_.neighbourPacketsCount) << "\n"
          << std::setw(hl) << "Polling frequency:" << std::setw(vl) << syncData_.sequencesVerificationFrequency;
}

void cs::PoolSynchronizer::processingSync(cs::RoundNumber roundNum, bool isBigBand) {
  if (transport_->getNeighboursCount() == 0) {
    cslog() << "POOL SYNCHRONIZER> Cannot start sync (no neighbours). Needed sequence: " << roundNum
            << ",   Requested pools block size:" << syncData_.blockPoolsCount;
    return;
  }

  const cs::Sequence lastWrittenSequence = blockChain_->getLastWrittenSequence();

  if (lastWrittenSequence >= roundNum) {
    const bool isFinished = showSyncronizationProgress(lastWrittenSequence);
    if (isFinished) {
      synchroFinished();
    }
    return;
  }

  if (!isSyncroStarted_ && roundNum < (lastWrittenSequence + roundDifferentForSync_)) {
    return;
  }

  csprint();

  if (isSyncroStarted_ && roundNum > 0) {
    --roundNum;
  }

  const cs::Sequence last = lastWrittenSequence + blockChain_->getCachedBlocksSize();
  const cs::Sequence blocksRemaining = roundNum - last;
  cslog() << "POOL SYNCHRONIZER> Blocks remaining: " << blocksRemaining;

  if (blocksRemaining == 0) {
    synchroFinished();
    return;
  }

  const bool useTimer = syncData_.sequencesVerificationFrequency > 1;
  const uint32_t delay =
      useTimer ? cs::numeric_cast<uint32_t>(syncData_.sequencesVerificationFrequency) : cs::NeighboursRequestDelay;

  // already synchro start, no BigBang, but no use timer
  if (isSyncroStarted_ && !useTimer && !isBigBand && timer_.isRunning()) {
    timer_.stop();
  }

  // already synchro start, but BigBang received
  if (isSyncroStarted_ && !useTimer && isBigBand && !timer_.isRunning()) {
    timer_.start(cs::numeric_cast<int>(delay));
  }

  if (!isSyncroStarted_) {
    isSyncroStarted_ = true;
    cs::Connector::connect(&blockChain_->writeBlockEvent, this, &cs::PoolSynchronizer::onWriteBlock);

    refreshNeighbours();
    sendBlockRequest();

    if (isBigBand || useTimer) {
      timer_.start(cs::numeric_cast<int>(delay));
    }
  }
  else if (syncData_.requestRepeatRoundCount > 0) {
    const bool isNeedRequest = checkActivity(CounterType::ROUND);
    bool isAvailable = false;

    if (syncData_.sequencesVerificationFrequency == 1) {
      isAvailable = checkActivity(CounterType::TIMER);
    }

    if (isNeedRequest || isAvailable) {
      sendBlockRequest();
    }
  }
}

void cs::PoolSynchronizer::getBlockReply(cs::PoolsBlock&& poolsBlock, std::size_t packetNum) {
  cslog() << "POOL SYNCHRONIZER> Get Block Reply <<<<<<< : count: " << poolsBlock.size() << ", seqs: ["
          << poolsBlock.front().sequence() << ", " << poolsBlock.back().sequence() << "], id: " << packetNum;

  /// TODO Fix numeric cast from RoundNum to cs::Sequence
  cs::Sequence lastWrittenSequence = blockChain_->getLastWrittenSequence();
  const cs::Sequence oldLastWrittenSequence = lastWrittenSequence;
  const std::size_t oldCachedBlocksSize = blockChain_->getCachedBlocksSize();

  for (auto& pool : poolsBlock) {
    const auto sequence = pool.sequence();

    checkNeighbourSequence(sequence);

    auto it = requestedSequences_.find(sequence);
    if (it != requestedSequences_.end()) {
      requestedSequences_.erase(it);
    }

    if (lastWrittenSequence > sequence) {
      continue;
    }

    if (blockChain_->storeBlock(pool, true /*by_sync*/)) {
      blockChain_->testCachedBlocks();
      lastWrittenSequence = blockChain_->getLastWrittenSequence();
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

  csprint() << "start";

  for (auto& neighbour : neighbours_) {
    if (!getNeededSequences(neighbour)) {
      csprint() << "neighbor: " << cs::numeric_cast<int>(neighbour.index()) << " is busy";
      continue;
    }

    if (neighbour.sequences().empty()) {
      csprint() << ">>> All sequences already requested";
      continue;
    }

    sendBlock(neighbour);
  }

  printNeighbours("Info:");
}

bool cs::PoolSynchronizer::isSyncroStarted() const {
  return isSyncroStarted_;
}

bool cs::PoolSynchronizer::isOneBlockReply() const {
  return syncData_.oneReplyBlock;
}

//
// Slots
//

void cs::PoolSynchronizer::onTimeOut() {
  CallsQueue::instance().insert([this] {
    if (!isSyncroStarted_) {
      return;
    }
    csprint() << "onTimeOut: " << syncData_.sequencesVerificationFrequency;
    const bool isAvailable = checkActivity(cs::PoolSynchronizer::CounterType::TIMER);

    if (isAvailable) {
      sendBlockRequest();
    }
  });
}

void cs::PoolSynchronizer::onWriteBlock(const cs::Sequence sequence) {
  auto it = requestedSequences_.find(sequence);

  if (it != requestedSequences_.end()) {
    checkNeighbourSequence(sequence);
    requestedSequences_.erase(it);
  }
}

//
// Service
//

bool cs::PoolSynchronizer::showSyncronizationProgress(const cs::Sequence lastWrittenSequence) const {
  const cs::RoundNumber globalSequence = cs::Conveyer::instance().currentRoundNumber();

  if (!globalSequence) {
    cswarning() << "POOL SYNCHRONIZER> " << __func__ << " Current round number: 0";
    return false;
  }

  const float last = float(lastWrittenSequence + blockChain_->getCachedBlocksSize());
  const float global = float(globalSequence - 1);
  const float maxValue = 100.0f;
  const uint32_t syncStatus = cs::numeric_cast<uint32_t>(std::min(((last / global) * maxValue), maxValue));
  const uint32_t remaining = cs::numeric_cast<uint32_t>(global - last);

  ProgressBar bar;
  cslog() << "SYNC: Blocks remaining: " << remaining << "\n\n";
  cslog() << "SYNC: " << bar.string(syncStatus) << "\n";

  return remaining == 0;
}

bool cs::PoolSynchronizer::checkActivity(const CounterType& counterType) {
  refreshNeighbours();

  if (neighbours_.empty()) {
    csprint() << " neighbours count is 0";
    return false;
  }

  csprint() << counterType;
  bool isNeedRequest = false;

  switch (counterType) {
    case CounterType::ROUND:
      printNeighbours("Activity:");
      for (auto& neighbour : neighbours_) {
        neighbour.increaseRoundCounter();
        if (!isNeedRequest && isAvailableRequest(neighbour)) {
          isNeedRequest = true;
        }
      }
      break;
    case CounterType::TIMER:
      for (auto& neighbour : neighbours_) {
        if (isNeedRequest) {
          break;
        }
        isNeedRequest = neighbour.sequences().empty();
      }
      break;
  }

  return isNeedRequest;
}

void cs::PoolSynchronizer::sendBlock(const NeighboursSetElemet& neighbour) {
  ConnectionPtr target = transport_->getNeighbourByNumber(neighbour.index());

  if (!target) {
    cserror() << "POOL SYNCHRONIZER> " << __func__ << " : Target is not valid";
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

  cslog() << "POOL SYNCHRONIZER> Sending block request >>>>>>> target: " << target->getOut() << " sequences ("
          << sequences.size() << "): [" << sequences.front() << ", " << sequences.back() << "], id: " << packet;

  emit sendRequest(target, sequences, packet);
}

bool cs::PoolSynchronizer::getNeededSequences(NeighboursSetElemet& neighbour) {
  const bool isLastPacket = isLastRequest();
  if (isLastPacket && !requestedSequences_.empty()) {
    csprint() << "Is last packet: requested sequences: [" << requestedSequences_.begin()->first << ", "
              << requestedSequences_.rbegin()->first << "]";

    const auto& sequences = neighbour.sequences();
    if (!sequences.empty() && requestedSequences_.find(sequences.front()) != requestedSequences_.end()) {
      csprint() << "Is last packet: this neighbour is already requested";
      if (isAvailableRequest(neighbour)) {
        neighbour.resetRoundCounter();
        return true;
      }
      return false;
    }

    neighbour.reset();
    const int nhIdx = cs::numeric_cast<int>(neighbour.index());
    for (const auto& [sequence, packet] : requestedSequences_) {
      (void)packet;
      neighbour.addSequences(sequence);
      csprint() << "Is last packet: nh: " << nhIdx << ", add seq: " << sequence;
    }
    csprint() << "Needed sequences size: " << neighbour.sequences().size();
    return true;
  }

  const std::vector<BlockChain::SequenceInterval> requiredBlocks = blockChain_->getRequiredBlocks();
  const cs::Sequence lastWrittenSequence = blockChain_->getLastWrittenSequence();

  for (const auto& el : requiredBlocks) {
    csprint() << "requiredBlocks: [" << el.first << ", " << el.second << "]";
  }

  if (!requestedSequences_.empty()) {
    // remove unnecessary sequnces
    requestedSequences_.erase(requestedSequences_.begin(), requestedSequences_.upper_bound(lastWrittenSequence));
  }
  else {
    csprint() << "Requested storage: size: 0";
  }

  cs::Sequence sequence = lastWrittenSequence;

  auto isNeededHelpIt = requestedSequences_.end();
  if (syncData_.neighbourPacketsCount > 0 && !isLastPacket) {
    isNeededHelpIt = std::find_if(requestedSequences_.begin(), requestedSequences_.end(),
                                  [this](const auto& pair) { return pair.second >= syncData_.neighbourPacketsCount; });
  }

  // if storage requested sequences is impty
  if (requestedSequences_.empty()) {
    sequence = lastWrittenSequence;
    csprint() << "From blockchain: " << sequence;
  }
  // Needed help another neighbour
  else if (isNeededHelpIt != requestedSequences_.end()) {
    sequence = isNeededHelpIt->first;

    csprint() << "From needed help: " << sequence;

    if (!neighbour.sequences().empty() && sequence != neighbour.sequences().front()) {
      for (const auto& seq : neighbour.sequences()) {
        requestedSequences_.erase(seq);
      }
      neighbour.reset();
    }

    auto needyNeighbour =
        std::find_if(neighbours_.begin(), neighbours_.end(), [sequence](const NeighboursSetElemet& el) {
          return (!el.sequences().empty() && el.sequences().front() == sequence);
        });

    neighbour.setSequences(needyNeighbour->sequences());
    return true;
  }
  // Repeat request
  else if (isAvailableRequest(neighbour)) {
    csprint() << "From repeat request: [" << neighbour.sequences().front() << ", " << neighbour.sequences().back()
              << "]";
    neighbour.resetRoundCounter();
    return true;
  }
  else {
    sequence = std::max(requestedSequences_.rbegin()->first, lastWrittenSequence);
    csprint() << "From other: " << sequence;
  }

  if (!neighbour.sequences().empty()) {
    return false;
  }

  csprint() << "Begin needed request Sequence: " << sequence;

  neighbour.resetSequences();

  for (std::size_t i = 0; i < syncData_.blockPoolsCount; ++i) {
    ++sequence;
    csprint() << "Need new sequence: " << sequence;

    // max sequence
    if (requiredBlocks.back().second != 0 && sequence > requiredBlocks.back().second) {
      csprint() << "Max sequence ";
      break;
    }

    for (std::size_t j = 1; j < requiredBlocks.size(); ++j) {
      // Within a valid pair
      if (sequence > requiredBlocks[j].first && sequence < requiredBlocks[j].second) {
        csprint() << "Check sequence interval: seq: " << sequence << ", Within a valid pair (" << j << "): ["
                  << requiredBlocks[j].first << ", " << requiredBlocks[j].second << "]";
        break;
      }
      // Between pairs
      if (sequence > requiredBlocks[j - 1].second && sequence < requiredBlocks[j].first) {
        sequence = requiredBlocks[j].first;
        csprint() << "Between pairs: " << sequence;
        break;
      }
    }

    neighbour.addSequences(sequence);
  }

  return true;
}

void cs::PoolSynchronizer::checkNeighbourSequence(const cs::Sequence sequence) {
  csprint() << sequence;

  for (auto& neighbour : neighbours_) {
    neighbour.removeSequnce(sequence);

    if (neighbour.sequences().empty()) {
      neighbour.resetRoundCounter();
    }
  }

  std::sort(neighbours_.begin(), neighbours_.end());

  printNeighbours("Check seq:");
}

void cs::PoolSynchronizer::refreshNeighbours() {
  const uint32_t neededNeighboursCount = transport_->getNeighboursCountWithoutSS();
  const auto nSize = neighbours_.size();

  if (nSize == neededNeighboursCount) {
    return;
  }

  csprint() << "Neighbours count without ss: " << neededNeighboursCount;

  const uint32_t allNeighboursCount = transport_->getNeighboursCount();

  // Add new neighbours
  if (nSize < neededNeighboursCount) {
    for (uint8_t i = cs::numeric_cast<uint8_t>(nSize); i < cs::numeric_cast<uint8_t>(allNeighboursCount); ++i) {
      ConnectionPtr neighbour = transport_->getNeighbourByNumber(i);
      if (neighbour && !neighbour->isSignal && neighbour->lastSeq) {
        auto isAlreadyHave =
            std::find_if(neighbours_.begin(), neighbours_.end(), [=](const auto& el) { return el.index() == i; });
        if (isAlreadyHave == neighbours_.end()) {
          neighbours_.emplace_back(NeighboursSetElemet(i, syncData_.blockPoolsCount));
        }
      }
    }
    csprint() << "Neighbours saved count is: " << neighbours_.size();
    return;
  }

  // refresh neighbours index
  std::size_t currentNh = 0;
  for (uint8_t i = 0; i < cs::numeric_cast<uint8_t>(allNeighboursCount); ++i) {
    ConnectionPtr neighbour = transport_->getNeighbourByNumber(i);
    if (neighbour && !neighbour->isSignal) {
      neighbours_[currentNh].setIndex(i);
      ++currentNh;
    }
  }

  // remove extra neighbour
  for (std::size_t i = neededNeighboursCount; i < nSize; ++i) {
    const auto& seqs = neighbours_.back().sequences();
    for (const auto& seq : seqs) {
      requestedSequences_.erase(seq);
    }
    neighbours_.pop_back();
  }

  csprint() << "Neighbours saved count is: " << neighbours_.size();
  printNeighbours("Refresh:");
}

bool cs::PoolSynchronizer::isLastRequest() const {
  const auto sum = cs::Conveyer::instance().currentRoundNumber() - blockChain_->getLastWrittenSequence() -
                   blockChain_->getCachedBlocksSize();
  return sum <= syncData_.blockPoolsCount;
}

bool cs::PoolSynchronizer::isAvailableRequest(const cs::PoolSynchronizer::NeighboursSetElemet& nh) const {
  return nh.roundCounter() >= syncData_.requestRepeatRoundCount;
}

void cs::PoolSynchronizer::synchroFinished() {
  cs::Connector::disconnect(blockChain_->writeBlockEvent);
  if (timer_.isRunning()) {
    timer_.stop();
  }
  isSyncroStarted_ = false;
  requestedSequences_.clear();
  neighbours_.clear();

  cslog() << "POOL SYNCHRONIZER> Synchro finished";
}

void cs::PoolSynchronizer::printNeighbours(const std::string& funcName) const {
  for (const auto& neighbour : neighbours_) {
    ConnectionPtr target = transport_->getNeighbourByNumber(neighbour.index());

    csdebug() << "POOL SYNCHRONIZER> " << funcName << " neighbour: " << target->getOut() << ", " << neighbour;
  }
}
