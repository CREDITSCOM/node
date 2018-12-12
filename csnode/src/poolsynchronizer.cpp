#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/utils.hpp>

#include <csnode/conveyer.hpp>

#include <net/transport.hpp>

cs::PoolSynchronizer::PoolSynchronizer(const PoolSyncData& data, Transport* transport, BlockChain* blockChain)
: m_syncData(data)
, m_transport(transport)
, m_blockChain(blockChain) {
  m_neighbours.reserve(m_transport->getMaxNeighbours());

  refreshNeighbours();

  cs::Connector::connect(&m_timer.timeOut, this, &cs::PoolSynchronizer::onTimeOut);

  // Print Pool Sync Data Info
  const uint8_t hl = 25;
  const uint8_t vl = 6;
  cslog() << "POOL SYNCHRONIZER> Pool sync data : \n"
          << std::setw(hl) << "Block pools:      " << std::setw(vl) << cs::numeric_cast<int>(m_syncData.blockPoolsCount)
          << "\n"
          << std::setw(hl) << "Request round:    " << std::setw(vl)
          << cs::numeric_cast<int>(m_syncData.requestRepeatRoundCount) << "\n"
          << std::setw(hl) << "Neighbour packets:" << std::setw(vl)
          << cs::numeric_cast<int>(m_syncData.neighbourPacketsCount) << "\n"
          << std::setw(hl) << "Polling frequency:" << std::setw(vl) << m_syncData.sequencesVerificationFrequency;
}

void cs::PoolSynchronizer::processingSync(cs::RoundNumber roundNum, bool isBigBand) {
  if (m_transport->getNeighboursCount() == 0) {
    cslog() << "POOL SYNCHRONIZER> Cannot start sync (no neighbours). Needed sequence: " << roundNum
            << ",   Requested pools block size:" << m_syncData.blockPoolsCount;
    return;
  }

  const auto lastWrittenSequence = m_blockChain->getLastWrittenSequence();

  if (lastWrittenSequence >= roundNum) {
    const bool isFinished = showSyncronizationProgress(cs::numeric_cast<csdb::Pool::sequence_t>(lastWrittenSequence));
    if (isFinished) {
      synchroFinished();
    }
    return;
  }

  if (!m_isSyncroStarted && roundNum < (lastWrittenSequence + s_roundDifferentForSync)) {
    return;
  }

  csprint();

  if (m_isSyncroStarted && roundNum > 0) {
    --roundNum;
  }

  const auto last = cs::numeric_cast<uint32_t>(lastWrittenSequence + m_blockChain->getCachedBlocksSize());
  cslog() << "POOL SYNCHRONIZER> Blocks remaining: " << roundNum - last;

  const bool useTimer = m_syncData.sequencesVerificationFrequency > 1;
  const uint32_t delay = useTimer ? cs::numeric_cast<uint32_t>(m_syncData.sequencesVerificationFrequency) : cs::NeighboursRequestDelay;

  // already synchro start, no BigBang, but no use timer
  if (m_isSyncroStarted && !useTimer && !isBigBand && m_timer.isRunning()) {
    m_timer.stop();
  }

  // already synchro start, but BigBang received
  if (m_isSyncroStarted && !useTimer && isBigBand && !m_timer.isRunning()) {
    m_timer.start(cs::numeric_cast<int>(delay));
  }

  if (!m_isSyncroStarted) {
    m_isSyncroStarted = true;

    refreshNeighbours();
    sendBlockRequest();

    if (isBigBand || useTimer) {
      m_timer.start(cs::numeric_cast<int>(delay));
    }
  }
  else if (m_syncData.requestRepeatRoundCount > 0) {
    const bool isNeedRequest = checkActivity(CounterType::ROUND);
    bool isAvailable = false;

    if (m_syncData.sequencesVerificationFrequency == 1) {
      isAvailable = checkActivity(CounterType::TIMER);
    }

    if (isNeedRequest || isAvailable) {
      sendBlockRequest();
    }
  }
}

void cs::PoolSynchronizer::getBlockReply(cs::PoolsBlock&& poolsBlock, uint32_t packet) {
  cslog() << "POOL SYNCHRONIZER> Get Block Reply <<<<<<< : count: " << poolsBlock.size() << ", seqs: ["
          << poolsBlock.front().sequence() << ", " << poolsBlock.back().sequence() << "], id: " << packet;

  /// TODO Fix numeric cast from RoundNum to csdb::Pool::sequence_t
  csdb::Pool::sequence_t lastWrittenSequence =
    cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

  if (lastWrittenSequence >= poolsBlock.back().sequence()) {
    return;
  }

  const std::size_t oldCachedBlocksSize = m_blockChain->getCachedBlocksSize();
  const csdb::Pool::sequence_t oldLastWrittenSequence =
    cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

  for (auto& pool : poolsBlock) {
    const auto sequence = pool.sequence();

    checkNeighbourSequence(sequence);

    auto it = m_requestedSequences.find(sequence);
    if (it != m_requestedSequences.end()) {
      m_requestedSequences.erase(it);
    }

    if (lastWrittenSequence > sequence) {
      continue;
    }

    if (m_blockChain->getGlobalSequence() < sequence) {
      m_blockChain->setGlobalSequence(cs::numeric_cast<uint32_t>(sequence));
    }

    if (m_blockChain->storeBlock(pool, true /*by_sync*/)) {
      m_blockChain->testCachedBlocks();
      lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());
    }
  }

  if (oldCachedBlocksSize != m_blockChain->getCachedBlocksSize() || oldLastWrittenSequence != lastWrittenSequence) {
    const bool isFinished = showSyncronizationProgress(lastWrittenSequence);
    if (isFinished) {
      synchroFinished();
    }
  }
}

void cs::PoolSynchronizer::sendBlockRequest() {
  if (m_neighbours.empty()) {
    return;
  }

  csprint() << "start";

  for (auto& neighbour : m_neighbours) {
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
  return m_isSyncroStarted;
}

//
// Slots
//

void cs::PoolSynchronizer::onTimeOut() {
  CallsQueue::instance().insert([this] {
    if (!m_isSyncroStarted) {
      return;
    }
    csprint() << "onTimeOut: " << m_syncData.sequencesVerificationFrequency;
    const bool isAvailable = checkActivity(cs::PoolSynchronizer::CounterType::TIMER);

    if (isAvailable) {
      sendBlockRequest();
    }
  });
}

//
// Service
//

bool cs::PoolSynchronizer::showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence) const {
  const csdb::Pool::sequence_t globalSequence =
    cs::numeric_cast<csdb::Pool::sequence_t>(cs::Conveyer::instance().currentRoundNumber());

  if (!globalSequence) {
    cswarning() << "POOL SYNCHRONIZER> " << __func__ << " Current round number: 0";
    return false;
  }

  const float last = float(lastWrittenSequence + m_blockChain->getCachedBlocksSize());
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

  if (m_neighbours.empty()) {
    return false;
  }

  csprint() << counterType;
  bool isNeedRequest = false;

  for (auto& neighbour : m_neighbours) {
    switch (counterType) {
    case CounterType::ROUND: {
      neighbour.increaseRoundCounter();
      if (!isNeedRequest && isAvailableRequest(neighbour)) {
        isNeedRequest = true;
      }
      break;
    }
    case CounterType::TIMER: {
      if (!isNeedRequest && neighbour.sequences().empty()) {
        isNeedRequest = true;
      }
      break;
    }
    }
  }

  printNeighbours("Activity:");

  return isNeedRequest;
}

void cs::PoolSynchronizer::sendBlock(const NeighboursSetElemet& neighbour) {
  ConnectionPtr target = m_transport->getNeighbourByNumber(neighbour.index());

  if (!target) {
    cserror() << "POOL SYNCHRONIZER> " << __func__ << " : Target is not valid";
    return;
  }

  uint32_t packet = 0;
  const auto& sequences = neighbour.sequences();

  for (const auto& sequence : sequences) {
    if (!m_requestedSequences.count(sequence)) {
      m_requestedSequences.emplace(std::make_pair(sequence, 0));
    }
    packet = ++(m_requestedSequences.at(sequence));
  }

  cslog() << "POOL SYNCHRONIZER> Sending block request >>>>>>> target: " << target->getOut() << " sequences ("
          << sequences.size() << "): [" << sequences.front() << ", " << sequences.back() << "], id: " << packet;

  emit sendRequest(target, sequences, packet);
}

bool cs::PoolSynchronizer::getNeededSequences(NeighboursSetElemet& neighbour) {
  const bool isLastPacket = isLastRequest();
  if (isLastPacket && !m_requestedSequences.empty()) {
    csprint() << "Is last packet: requested sequences: [" << m_requestedSequences.begin()->first << ", "
              << m_requestedSequences.rbegin()->first << "]";

    const auto& sequences = neighbour.sequences();
    if (!sequences.empty() && m_requestedSequences.find(cs::numeric_cast<csdb::Pool::sequence_t>(sequences.front())) !=
          m_requestedSequences.end()) {
      csprint() << "Is last packet: this neighbour is already requested";
      if (isAvailableRequest(neighbour)) {
        neighbour.resetRoundCounter();
        return true;
      }
      return false;
    }

    neighbour.reset();
    const int nhIdx = cs::numeric_cast<int>(neighbour.index());
    for (const auto& [sequence, packet] : m_requestedSequences) {
      (void)packet;
      neighbour.addSequences(cs::numeric_cast<cs::RoundNumber>(sequence));
      csprint() << "Is last packet: nh: " << nhIdx << ", add seq: " << sequence;
    }
    csprint() << "Needed sequences size: " << neighbour.sequences().size();
    return true;
  }

  const std::vector<BlockChain::SequenceInterval> requiredBlocks = m_blockChain->getRequiredBlocks();
  const csdb::Pool::sequence_t lastWrittenSequence =
    cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

  for (const auto& el : requiredBlocks) {
    csprint() << "requiredBlocks: [" << el.first << ", " << el.second << "]";
  }

  if (!m_requestedSequences.empty()) {
    // remove unnecessary sequnces
    m_requestedSequences.erase(m_requestedSequences.begin(), m_requestedSequences.upper_bound(lastWrittenSequence));
  }
  else {
    csprint() << "Requested storage: size: 0";
  }

  csdb::Pool::sequence_t sequence = lastWrittenSequence;

  auto isNeededHelpIt = m_requestedSequences.end();
  if (m_syncData.neighbourPacketsCount > 0 && !isLastPacket) {
    isNeededHelpIt = std::find_if(m_requestedSequences.begin(), m_requestedSequences.end(),
                                  [this](const auto& pair) { return pair.second >= m_syncData.neighbourPacketsCount; });
  }

  // if storage requested sequences is impty
  if (m_requestedSequences.empty()) {
    sequence = lastWrittenSequence;
    csprint() << "From blockchain: " << sequence;
  }
  // Needed help another neighbour
  else if (isNeededHelpIt != m_requestedSequences.end()) {
    sequence = isNeededHelpIt->first;

    csprint() << "From needed help: " << sequence;

    if (!neighbour.sequences().empty() && sequence != neighbour.sequences().front()) {
      for (const auto& seq : neighbour.sequences()) {
        m_requestedSequences.erase(seq);
      }
      neighbour.reset();
    }

    auto needyNeighbour =
      std::find_if(m_neighbours.begin(), m_neighbours.end(), [sequence](const NeighboursSetElemet& el) {
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
    sequence = std::max(m_requestedSequences.rbegin()->first, lastWrittenSequence);
    csprint() << "From other: " << sequence;
  }

  if (!neighbour.sequences().empty()) {
    return false;
  }

  csprint() << "Begin needed request Sequence: " << sequence;

  neighbour.resetSequences();

  for (std::size_t i = 0; i < m_syncData.blockPoolsCount; ++i) {
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

    neighbour.addSequences(cs::numeric_cast<cs::RoundNumber>(sequence));
  }

  return true;
}

void cs::PoolSynchronizer::checkNeighbourSequence(const csdb::Pool::sequence_t sequence) {
  csprint() << sequence;

  for (auto& neighbour : m_neighbours) {
    neighbour.removeSequnce(sequence);

    if (neighbour.sequences().empty()) {
      neighbour.reset();
    }
  }

  std::sort(m_neighbours.begin(), m_neighbours.end());

  printNeighbours("Check seq:");
}

void cs::PoolSynchronizer::refreshNeighbours() {
  const uint32_t neededNeighboursCount = m_transport->getNeighboursCountWithoutSS();
  const auto nSize = m_neighbours.size();

  if (nSize == neededNeighboursCount) {
    return;
  }

  csprint() << "Neighbours count without ss: " << neededNeighboursCount;

  const uint32_t allNeighboursCount = m_transport->getNeighboursCount();

  // Add new neighbours
  if (nSize < neededNeighboursCount) {
    for (uint8_t i = cs::numeric_cast<uint8_t>(nSize); i < cs::numeric_cast<uint8_t>(allNeighboursCount); ++i) {
      ConnectionPtr neighbour = m_transport->getNeighbourByNumber(i);
      if (neighbour && !neighbour->isSignal && neighbour->lastSeq /* > m_blockChain->getLastWrittenSequence()*/) {
        auto isAlreadyHave =
          std::find_if(m_neighbours.begin(), m_neighbours.end(), [=](const auto& el) { return el.index() == i; });
        if (isAlreadyHave == m_neighbours.end()) {
          m_neighbours.emplace_back(NeighboursSetElemet(i, m_syncData.blockPoolsCount));
        }
      }
    }
    csprint() << "Neighbours saved count is: " << m_neighbours.size();
    return;
  }

  // refresh neighbours index
  std::size_t currentNh = 0;
  for (uint8_t i = 0; i < cs::numeric_cast<uint8_t>(allNeighboursCount); ++i) {
    ConnectionPtr neighbour = m_transport->getNeighbourByNumber(i);
    if (neighbour && !neighbour->isSignal) {
      m_neighbours[currentNh].setIndex(i);
      ++currentNh;
    }
  }

  // remove extra neighbour
  for (std::size_t i = neededNeighboursCount; i < nSize; ++i) {
    const auto& seqs = m_neighbours.back().sequences();
    for (const auto& seq : seqs) {
      m_requestedSequences.erase(seq);
    }
    m_neighbours.pop_back();
  }

  csprint() << "Neighbours saved count is: " << m_neighbours.size();
  printNeighbours("Refresh:");
}

bool cs::PoolSynchronizer::isLastRequest() const {
  const auto sum = cs::Conveyer::instance().currentRoundNumber() - m_blockChain->getLastWrittenSequence() -
    cs::numeric_cast<uint32_t>(m_blockChain->getCachedBlocksSize());
  return sum <= m_syncData.blockPoolsCount;
}

bool cs::PoolSynchronizer::isAvailableRequest(const cs::PoolSynchronizer::NeighboursSetElemet& nh) const {
  return nh.roundCounter() >= m_syncData.requestRepeatRoundCount;
}

void cs::PoolSynchronizer::synchroFinished() {
  if (m_timer.isRunning()) {
    m_timer.stop();
  }
  m_isSyncroStarted = false;
  m_requestedSequences.clear();
  m_neighbours.clear();

  cslog() << "POOL SYNCHRONIZER> Synchro finished";
}

void cs::PoolSynchronizer::printNeighbours(const std::string& funcName) const {
  for (const auto& neighbour : m_neighbours) {
    ConnectionPtr target = m_transport->getNeighbourByNumber(neighbour.index());

    csdebug() << "POOL SYNCHRONIZER> " << funcName << " neighbour: " << target->getOut() << ", " << neighbour;
  }
}
