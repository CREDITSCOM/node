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
                  << std::setw(hl) << "Fast mode:        " << std::setw(vl) << syncData_.isFastMode << "\n"
                  << std::setw(hl) << "One reply block:  " << std::setw(vl) << syncData_.oneReplyBlock << "\n"
                  << std::setw(hl) << "Block pools:      " << std::setw(vl) << cs::numeric_cast<int>(syncData_.blockPoolsCount) << "\n"
                  << std::setw(hl) << "Request round:    " << std::setw(vl)
                  << cs::numeric_cast<int>(syncData_.requestRepeatRoundCount) << "\n"
                  << std::setw(hl) << "Neighbour packets:" << std::setw(vl)
                  << cs::numeric_cast<int>(syncData_.neighbourPacketsCount) << "\n"
                  << std::setw(hl) << "Polling frequency:" << std::setw(vl) << syncData_.sequencesVerificationFrequency;
}

void cs::PoolSynchronizer::processingSync(cs::RoundNumber roundNum, bool isBigBand) {
  if (transport_->getNeighboursCount() == 0) {
    csmeta(csdebug) << "Cannot start sync (no neighbours). Needed sequence: " << roundNum
                    << ",   Requested pools block size:" << syncData_.blockPoolsCount;
    return;
  }

  const cs::Sequence lastWrittenSequence = blockChain_->getLastSequence();

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

  csmeta(csdetails) << "Started";

  if (isSyncroStarted_ && roundNum > 0) {
    --roundNum;
  }

  const std::size_t cachedBlocksSize = blockChain_->getCachedBlocksSize();
  const cs::Sequence totalBlocks = lastWrittenSequence + cachedBlocksSize;
  const cs::Sequence blocksRemaining = roundNum - totalBlocks;
  csdebug() << "SYNC:\n Round num: " << roundNum
            << "\n Conveyer round: " << cs::Conveyer::instance().currentRoundNumber()
            << "\n Last written seq: " << lastWrittenSequence
            << "\n Cached blocks: " << cachedBlocksSize
            << "\n Total blocks: " << totalBlocks;
  cslog() << "SYNC: Blocks remaining: " << blocksRemaining;

  if (blocksRemaining == 0) {
    showSyncronizationProgress(lastWrittenSequence);
    synchroFinished();
    return;
  }

  const bool useTimer = syncData_.sequencesVerificationFrequency > 1;
  const int delay = useTimer ?
                    cs::numeric_cast<int>(syncData_.sequencesVerificationFrequency) :
                    cs::numeric_cast<int>(cs::NeighboursRequestDelay);

  // already synchro start
  if (isSyncroStarted_ && !useTimer) {
    // no BigBang, but no use timer
    if (!isBigBand && timer_.isRunning()) {
      timer_.stop();
    }

    // BigBang received
    if (isBigBand && !timer_.isRunning()) {
      timer_.start(delay);
    }
  }

  if (!isSyncroStarted_) {
    isSyncroStarted_ = true;
    cs::Connector::connect(&blockChain_->writeBlockEvent, this, &cs::PoolSynchronizer::onWriteBlock);
    cs::Connector::connect(&blockChain_->removeBlockEvent, this, &cs::PoolSynchronizer::onRemoveBlock);

    refreshNeighbours();
    sendBlockRequest();

    if (isBigBand || useTimer) {
      timer_.start(delay);
    }

    roundSimulation_.start(60000, cs::Timer::Type::HighPrecise); // 1 Min
  }
  else if (syncData_.requestRepeatRoundCount > 0) {
    roundSimulation_.restart();
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
  csmeta(csdebug) << "Get Block Reply <<<<<<< : count: " << poolsBlock.size() << ", seqs: ["
                  << poolsBlock.front().sequence() << ", " << poolsBlock.back().sequence() << "], id: " << packetNum;

  /// TODO Fix numeric cast from RoundNum to cs::Sequence
  cs::Sequence lastWrittenSequence = blockChain_->getLastSequence();
  const cs::Sequence oldLastWrittenSequence = lastWrittenSequence;
  const std::size_t oldCachedBlocksSize = blockChain_->getCachedBlocksSize();

  // TODO Think, do really need this here?!
  // refreshNeighbours();

  for (auto& pool : poolsBlock) {
    const auto sequence = pool.sequence();

    removeExistingSequence(sequence);

    if (lastWrittenSequence > sequence) {
      continue;
    }

    if (blockChain_->storeBlock(pool, true /*by_sync*/)) {
      blockChain_->testCachedBlocks();
      lastWrittenSequence = blockChain_->getLastSequence();
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

  bool success = false;

  for (auto& neighbour : neighbours_) {
    if (!getNeededSequences(neighbour)) {
      csmeta(csdetails) << "Neighbor: " << cs::numeric_cast<int>(neighbour.index()) << " is busy";
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
  return syncData_.oneReplyBlock;
}

bool cs::PoolSynchronizer::isFastMode() const {
  if (!isSyncroStarted_ || !syncData_.isFastMode) {
    return false;
  }

  const cs::Sequence sum = cs::Conveyer::instance().currentRoundNumber() - blockChain_->getLastSequence() -
                           blockChain_->getCachedBlocksSize();
  return sum > cs::numeric_cast<cs::Sequence>(syncData_.blockPoolsCount * 3);  // roundDifferentForSync_
}

//
// Slots
//

void cs::PoolSynchronizer::onTimeOut() {
  CallsQueue::instance().insert([this] {
    if (!isSyncroStarted_) {
      return;
    }

    bool isAvailable = false;

    if (isFastMode()) {
      static uint8_t fastCounter = 0;
      ++fastCounter;
      if (fastCounter > 20) {
        fastCounter = 0;
        csmeta(csdetails) << "OnTimeOut Fast: " << syncData_.sequencesVerificationFrequency * 20;
        isAvailable = checkActivity(cs::PoolSynchronizer::CounterType::ROUND);
      }
    }

    if (!isAvailable) {
      csmeta(csdetails) << "OnTimeOut: " << syncData_.sequencesVerificationFrequency;
      isAvailable = checkActivity(cs::PoolSynchronizer::CounterType::TIMER);
    }

    if (isAvailable) {
      sendBlockRequest();
    }
  });
}

void cs::PoolSynchronizer::onRoundSimulation() {
  csmeta(csdetails) << "on round simulation";
  bool isAvailable = checkActivity(cs::PoolSynchronizer::CounterType::ROUND);

  if (isAvailable) {
    sendBlockRequest();
  }
}

void cs::PoolSynchronizer::onWriteBlock(const cs::Sequence sequence) {
  removeExistingSequence(sequence);
}

void cs::PoolSynchronizer::onRemoveBlock(const cs::Sequence sequence) {
  csmeta(csdetails) << sequence;
  removeExistingSequence(sequence, SequenceRemovalAccuracy::UPPER_BOUND);
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
  const float maxValue = 100.0f;
  const uint32_t syncStatus = cs::numeric_cast<uint32_t>(std::min(((last / global) * maxValue), maxValue));
  const uint32_t remaining = cs::numeric_cast<uint32_t>(global - last);

  ProgressBar bar;
  std::cout << "\n";
  cslog() << "SYNC: Blocks remaining: " << remaining;
  csdebug() << "SYNC: Cached blocks size in blockchain: " << cachedBlocksSize;
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
      printNeighbours("Activity Round:");
      break;
    case CounterType::TIMER:
      for (auto& neighbour : neighbours_) {
        isNeedRequest = neighbour.sequences().empty();
        if (isNeedRequest) {
          break;
        }
      }
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

  cslog() << "SYNC: Sending block request sequences (" << sequences.size() << "): ["
          << sequences.front() << ", " << sequences.back() << "], id: " << packet;

  csmeta(csdebug) << "Sending block request >>>>>>> target: " << target->getOut();

  emit sendRequest(target, sequences, packet);
}

bool cs::PoolSynchronizer::getNeededSequences(NeighboursSetElemet& neighbour) {
  const bool isLastPacket = isLastRequest();
  if (isLastPacket && !requestedSequences_.empty()) {
    csmeta(csdetails) << "Is last packet: requested sequences: [" << requestedSequences_.begin()->first << ", "
              << requestedSequences_.rbegin()->first << "]";

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
    const int nhIdx = cs::numeric_cast<int>(neighbour.index());
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

  const cs::Sequence lastWrittenSequence = blockChain_->getLastSequence();

  // remove unnecessary sequnces
  removeExistingSequence(lastWrittenSequence, SequenceRemovalAccuracy::LOWER_BOUND);

  cs::Sequence sequence = lastWrittenSequence;

  auto isNeededHelpIt = requestedSequences_.end();
  if (syncData_.neighbourPacketsCount > 0 && !isLastPacket) {
    isNeededHelpIt = std::find_if(requestedSequences_.begin(), requestedSequences_.end(),
                                  [this](const auto& pair) { return pair.second >= syncData_.neighbourPacketsCount; });
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

    auto needyNeighbour =
        std::find_if(neighbours_.begin(), neighbours_.end(), [sequence](const NeighboursSetElemet& el) {
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
  // Repeat request
  else if (isAvailableRequest(neighbour)) {
    csmeta(csdetails) << "From repeat request: [" << neighbour.sequences().front() << ", " << neighbour.sequences().back()
              << "]";
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

  for (std::size_t i = 0; i < syncData_.blockPoolsCount; ++i) {
    ++sequence;

    // max sequence
    if (requiredBlocks.back().second != 0 && sequence > requiredBlocks.back().second) {
      csmeta(csdetails) << "Max sequence reached";
      break;
    }

    for (std::size_t j = 1; j < requiredBlocks.size(); ++j) {
      // Within a valid pair
      if (sequence > requiredBlocks[j].first && sequence < requiredBlocks[j].second) {
        csmeta(csdetails) << "Check sequence interval: seq: " << sequence << ", Within a valid pair (" << j << "): ["
                  << requiredBlocks[j].first << ", " << requiredBlocks[j].second << "]";
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

    printNeighbours("Check seq:");
  }
}

void cs::PoolSynchronizer::removeExistingSequence(const cs::Sequence sequence, const SequenceRemovalAccuracy accuracy) {
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
  const uint8_t neededNeighboursCount = cs::numeric_cast<uint8_t>(transport_->getNeighboursCountWithoutSS());
  const uint8_t nSize = cs::numeric_cast<uint8_t>(neighbours_.size());

  if (nSize == neededNeighboursCount) {
    return;
  }

  csmeta(csdetails) << "Neighbours count without ss: " << cs::numeric_cast<int>(neededNeighboursCount);

  const uint8_t allNeighboursCount = cs::numeric_cast<uint8_t>(transport_->getNeighboursCount());

  // Add new neighbours
  if (nSize < neededNeighboursCount) {
    for (uint8_t i = nSize; i < allNeighboursCount; ++i) {
      ConnectionPtr neighbour = transport_->getConnectionByNumber(i);
      if (neighbour && !neighbour->isSignal && neighbour->lastSeq) {
        auto isAlreadyHave = std::find_if(neighbours_.begin(), neighbours_.end(), [=](const auto& el) {
          return el.index() == i;
        });

        if (isAlreadyHave == neighbours_.end()) {
          neighbours_.emplace_back(NeighboursSetElemet(i, neighbour->key, syncData_.blockPoolsCount));
        }
      }
    }
    csmeta(csdetails) << "Neighbours saved count is: " << neighbours_.size();
    return;
  }

  // refresh neighbours index
  std::size_t currentNh = 0;
  for (uint8_t i = 0; i < allNeighboursCount; ++i) {
    ConnectionPtr neighbour = transport_->getConnectionByNumber(i);
    if (neighbour && !neighbour->isSignal) {
      neighbours_[currentNh].setIndex(i);
      neighbours_[currentNh].setPublicKey(neighbour->key);
      ++currentNh;
    }
  }

  // remove extra neighbour
  for (uint8_t i = neededNeighboursCount; i < nSize; ++i) {
    const auto& seqs = neighbours_.back().sequences();
    for (const auto& seq : seqs) {
      requestedSequences_.erase(seq);
    }
    neighbours_.pop_back();
  }

  csmeta(csdetails) << "Neighbours saved count is: " << neighbours_.size();
  printNeighbours("Refresh:");
}

bool cs::PoolSynchronizer::isLastRequest() const {
  const auto sum = cs::Conveyer::instance().currentRoundNumber() - blockChain_->getLastSequence() -
                   blockChain_->getCachedBlocksSize();
  return sum <= syncData_.blockPoolsCount;
}

bool cs::PoolSynchronizer::isAvailableRequest(const cs::PoolSynchronizer::NeighboursSetElemet& nh) const {
  return nh.roundCounter() >= syncData_.requestRepeatRoundCount;
}

void cs::PoolSynchronizer::synchroFinished() {
  cs::Connector::disconnect(&blockChain_->writeBlockEvent);
  cs::Connector::disconnect(&blockChain_->removeBlockEvent);
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
      csmeta(csdebug) << funcName
                      << " Neighbour: " << target->getOut()
                      << ", " << neighbour;
    }
    else {
      csmeta(csdebug) << funcName
                      << " Neighbour index: " << neighbour.index()
                      << ", does not contained in transport. Neighbours сount: " << transport_->getNeighboursCount()
                      << ", neighbour key: "
                      << cs::Utils::byteStreamToHex(neighbour.publicKey().data(), neighbour.publicKey().size());
    }
  }
}
