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
  m_neededSequences.reserve(m_syncData.blockPoolsCount);
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

void cs::PoolSynchronizer::processingSync(const cs::RoundNumber roundNum, bool isBigBand) {
  if (m_transport->getNeighboursCount() == 0) {
    cslog() << "POOL SYNCHRONIZER> Cannot start sync (no neighbours). Needed sequence: " << roundNum
            << ",   Requested pools block size:" << m_syncData.blockPoolsCount;
    return;
  }

  const auto lastWrittenSequence = m_blockChain->getLastWrittenSequence();

  // TODO: review
  auto roundToSync = roundNum;
  if(m_isSyncroStarted && roundNum > 0) {
    --roundToSync;
  }

  if (lastWrittenSequence >= roundToSync) {
    showSyncronizationProgress(cs::numeric_cast<csdb::Pool::sequence_t>(lastWrittenSequence));
    return;
  }

  if (!m_isSyncroStarted && roundNum < (lastWrittenSequence + s_roundDifferentForSync)) {
    return;
  }

  csdebug() << "POOL SYNCHRONIZER> Processing sync";

  const auto last = cs::numeric_cast<uint32_t>(lastWrittenSequence + m_blockChain->getCachedBlocksSize());
  cslog() << "POOL SYNCHRONIZER> Blocks remaining: " << roundToSync - last;

  const bool useTimer = m_syncData.sequencesVerificationFrequency > 1;

  // already synchro start, no BigBang, but no use timer
  if (m_isSyncroStarted && !useTimer && !isBigBand && m_timer.isRunning()) {
    m_timer.stop();
  }

  // already synchro start, but BigBang received
  if (m_isSyncroStarted && !useTimer && isBigBand && !m_timer.isRunning()) {
    const uint32_t delay =
        useTimer ? cs::numeric_cast<uint32_t>(m_syncData.sequencesVerificationFrequency) : cs::NeighboursRequestDelay;
    m_timer.start(delay);
  }

  if (!m_isSyncroStarted) {
    m_isSyncroStarted = true;

    refreshNeighbours();
    sendBlockRequest();

    if (isBigBand || useTimer) {
      const uint32_t delay =
          useTimer ? cs::numeric_cast<uint32_t>(m_syncData.sequencesVerificationFrequency) : cs::NeighboursRequestDelay;
      m_timer.start(delay);
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
  cslog() << "POOL SYNCHRONIZER> Get Block Reply <<<<<<< : count: " << poolsBlock.size() << ", seqs: [" << poolsBlock.front().sequence()
          << ", " << poolsBlock.back().sequence() << "],   id: " << packet;

  /// TODO Fix numeric cast from RoundNum to csdb::Pool::sequence_t
  csdb::Pool::sequence_t lastWrittenSequence =
      cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

  if (lastWrittenSequence >= poolsBlock.back().sequence()) {
    return;
  }

  const std::size_t oldCachedBlocksSize = m_blockChain->getCachedBlocksSize();
  const csdb::Pool::sequence_t oldLastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

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

  lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

  if (oldCachedBlocksSize != m_blockChain->getCachedBlocksSize() || oldLastWrittenSequence != lastWrittenSequence) {
    showSyncronizationProgress(lastWrittenSequence);
  }
}

void cs::PoolSynchronizer::sendBlockRequest() {
  if (m_neighbours.empty()) {
    return;
  }

  csdebug() << "POOL SYNCHRONIZER> send Block Request start";
  bool isLastReq = false;

  std::uint8_t idx = 0;
  for (auto& neighbour : m_neighbours) {
    if (!isLastReq && !getNeededSequences(idx++)) {
      isLastReq = isLastRequest();
      csdebug() << "POOL SYNCHRONIZER> All sequences already requested";
      continue;
    }

    if (m_neededSequences.empty()) {
      csdebug() << "POOL SYNCHRONIZER> >>> All sequences already requested";
      continue;
    }

    if (!isLastReq) {
      isLastReq = isLastRequest();
    }

    if (isLastReq && !m_neededSequences.empty()) {
      csdebug() << "POOL SYNCHRONIZER> Is last neeed sequnces: [" << m_neededSequences.front() << ", "
                << m_neededSequences.back() << "]";
    }

    if (isLastReq || neighbour.isAvailableSequences()) {
      neighbour.setSequences(m_neededSequences);
      sendBlock(neighbour.neighbourNum());
    }
    else if(isAvailableRequest(neighbour)) {
      neighbour.resetRoundCounter();
      sendBlock(neighbour.neighbourNum());
    }
  }

  printNeighbours("Info: ");
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
    csdebug() << "POOL SYNCHRONIZER> On time out: " << m_syncData.sequencesVerificationFrequency;
    const bool isAvailable = checkActivity(cs::PoolSynchronizer::CounterType::TIMER);

    if (isAvailable) {
      sendBlockRequest();
    }
  });
}

//
// Service
//

void cs::PoolSynchronizer::showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence) {
  const csdb::Pool::sequence_t globalSequence =
      cs::numeric_cast<csdb::Pool::sequence_t>(cs::Conveyer::instance().currentRoundNumber());

  if (!globalSequence) {
    return;
  }

  const auto last = float(lastWrittenSequence + m_blockChain->getCachedBlocksSize());
  const auto global = float(globalSequence - 1);
  const float maxValue = 100.0f;
  const uint32_t syncStatus = cs::numeric_cast<uint32_t>(std::min(((last / global) * maxValue), maxValue));

  const uint32_t remaining = cs::numeric_cast<uint32_t>(global - last);

  if (remaining == 0) {
    synchroFinished();
  }

  ProgressBar bar;
  cslog() << "SYNC: Blocks remaining: " << remaining << "\n\n";
  cslog() << "SYNC: " << bar.string(syncStatus) << "\n";
}

bool cs::PoolSynchronizer::checkActivity(const CounterType& counterType) {
  refreshNeighbours();

  if (m_neighbours.empty()) {
    return false;
  }

  csdebug() << "POOL SYNCHRONIZER> Check activity: " << counterType;
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
        if (!isNeedRequest && neighbour.isAvailableSequences()) {
          isNeedRequest = true;
        }
        break;
      }
      default:
        break;
    }
  }

  if (counterType == CounterType::ROUND) {
    printNeighbours("Check activity:");
  }

  return isNeedRequest;
}

void cs::PoolSynchronizer::sendBlock(uint8_t neighbourNum) {
  ConnectionPtr target = m_transport->getNeighbourByNumber(neighbourNum);

  if (!target) {
    cserror() << "POOL SYNCHRONIZER> Sending block request : Target is not valid";
    return;
  }

  uint32_t packet = 0;

  for (const auto& sequence : m_neededSequences) {
    if (!m_requestedSequences.count(sequence)) {
      m_requestedSequences.emplace(std::make_pair(sequence, 0));
    }
    packet = ++(m_requestedSequences.at(sequence));
  }

  cslog() << "POOL SYNCHRONIZER> Sending block request >>>>>>> target: " << target->getOut() <<" sequences (" << m_neededSequences.size() << "): ["
          << m_neededSequences.front() << ", " << m_neededSequences.back() << "],  id: " << packet;

  emit sendRequest(target, m_neededSequences, packet);
}

bool cs::PoolSynchronizer::getNeededSequences(uint8_t nieghbourNumber) {
  const bool isLastPacket = isLastRequest();
  if (isLastPacket && !m_requestedSequences.empty()) {
    m_neededSequences.clear();
    for (const auto& [sequence, packet] : m_requestedSequences) {
      m_neededSequences.push_back(cs::numeric_cast<cs::RoundNumber>(sequence));
      csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Is last packet: size: add seq: " << sequence;
    }
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: needed size: " << m_neededSequences.size();
    return !m_neededSequences.empty();
  }

  const std::vector<BlockChain::SequenceInterval> requiredBlocks = m_blockChain->getRequiredBlocks();
  const csdb::Pool::sequence_t lastWrittenSequence =
      cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

  if (!m_requestedSequences.empty()) {
    // remove unnecessary sequnces
    m_requestedSequences.erase(m_requestedSequences.begin(), m_requestedSequences.upper_bound(lastWrittenSequence));
  }
  else {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Requested storage: size: 0";
  }

  csdb::Pool::sequence_t sequence = lastWrittenSequence;

  auto isNeededHelpIt = m_requestedSequences.end();
  if (m_syncData.neighbourPacketsCount > 0 && !isLastPacket) {
    isNeededHelpIt = std::find_if(m_requestedSequences.begin(), m_requestedSequences.end(),
                                  [this](const auto& pair) { return pair.second >= m_syncData.neighbourPacketsCount; });
  }
  NeighboursSetElemet& nh = m_neighbours[nieghbourNumber];
  const bool isRepeatRequest = isAvailableRequest(nh);

  // if storage requested sequences is impty
  if (m_requestedSequences.empty()) {
    sequence = lastWrittenSequence;
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from blockchain: " << sequence;
  }
  // Needed help another neighbour
  else if (isNeededHelpIt != m_requestedSequences.end()) {
    sequence = isNeededHelpIt->first;

    for (const auto& el : m_requestedSequences) {
      csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Requested sequences storage: " << "seq: " << el.first << ", pack: " << el.second;
    }

    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from needed help: " << sequence;

    if (!nh.sequences().empty() && sequence != nh.sequences().front()) {
      for (const auto& seq : nh.sequences()) {
        m_requestedSequences.erase(seq);
      }
      nh.reset();
    }

    auto needyNeighbour = std::find_if(m_neighbours.begin(), m_neighbours.end(), [sequence](const NeighboursSetElemet& el) {
      return (!el.sequences().empty() && el.sequences().front() == sequence);
    });

    m_neededSequences.clear();
    m_neededSequences = needyNeighbour->sequences();
    return !m_neededSequences.empty();
  }
  // Repeat request
  else if (isRepeatRequest) {
    sequence = cs::numeric_cast<csdb::Pool::sequence_t>(nh.sequences().front());
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from repeat request: [" << nh.sequences().front() << ", " << nh.sequences().back() << "]";

    m_neededSequences.clear();
    m_neededSequences = nh.sequences();
    return true;
  }
  else {
    sequence = std::max(m_requestedSequences.rbegin()->first, lastWrittenSequence);
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from other: " << sequence;
  }

  if (!(isLastRequest() || nh.isAvailableSequences() || isAvailableRequest(nh))) {
    return !m_neededSequences.empty();
  }

  csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Begin needed request Sequence: " << sequence;

  m_neededSequences.clear();

  for (std::size_t i = 0; i < m_syncData.blockPoolsCount; ++i) {
    ++sequence;
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Needed new sequence: " << sequence;

    // max sequence
    if (requiredBlocks.back().second != 0 && sequence > requiredBlocks.back().second) {
      csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Max sequence ";
      break;
    }

    for (std::size_t j = 1; j < requiredBlocks.size(); ++j) {
      // Within a valid pair
      if (sequence > requiredBlocks[j].first && sequence < requiredBlocks[j].second) {
        csdebug() << "POOL SYNCHRONIZER> Check sequence interval: seq: " << sequence << ", Within a valid pair (" << j
                  << "): [" << requiredBlocks[j].first << ", " << requiredBlocks[j].second << "]";
        break;
      }
      // Between pairs
      if (sequence > requiredBlocks[j - 1].second && sequence < requiredBlocks[j].first) {
        sequence = requiredBlocks[j].first;
        csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Between pairs: " << sequence;
        break;
      }
    }

    m_neededSequences.push_back(cs::numeric_cast<cs::RoundNumber>(sequence));
  }

  return !m_neededSequences.empty();
}

void cs::PoolSynchronizer::checkNeighbourSequence(const csdb::Pool::sequence_t sequence) {
  csdebug() << "POOL SYNCHRONIZER> Check neighbour sequence: " << sequence;

  for (auto& neighbour : m_neighbours) {
    neighbour.removeSequnce(sequence);

    if (neighbour.sequences().empty()) {
      neighbour.reset();
    }
  }

  std::sort(m_neighbours.begin(), m_neighbours.end());

  printNeighbours("Check neighbour sequence>");
}

void cs::PoolSynchronizer::refreshNeighbours() {
  const uint32_t neededNeighboursCount = m_transport->getNeighboursCountWithoutSS();
  auto nSize = m_neighbours.size();

  if (nSize == neededNeighboursCount) {
    return;
  }

  csdebug() << "POOL SYNCHRONIZER> Neighbours count is: " << neededNeighboursCount;

  const uint32_t allNeighboursCount = m_transport->getNeighboursCount();

  // Add new neighbours
  if (nSize < neededNeighboursCount) {
    for (uint8_t i = cs::numeric_cast<uint8_t>(nSize); i < cs::numeric_cast<uint8_t>(allNeighboursCount); ++i) {
      ConnectionPtr neighbour = m_transport->getNeighbourByNumber(i);
      if (neighbour && !neighbour->isSignal && neighbour->lastSeq /* > m_blockChain->getLastWrittenSequence()*/) {
        auto isAlreadyHave = std::find_if(m_neighbours.begin(), m_neighbours.end(), [=](const auto& el) {
          return el.neighbourNum() == i;
        });
        if (isAlreadyHave == m_neighbours.end()) {
          m_neighbours.emplace_back(NeighboursSetElemet(i, m_syncData.blockPoolsCount));
        }
      }
    }
    csdebug() << "POOL SYNCHRONIZER> Neighbours saved count is: " << m_neighbours.size();
    return;
  }

  // refresh neighbours index
  std::size_t currentNh = 0;
  for (uint8_t i = 0; i < cs::numeric_cast<uint8_t>(allNeighboursCount); ++i) {
    ConnectionPtr neighbour = m_transport->getNeighbourByNumber(i);
    if (neighbour && !neighbour->isSignal) {
      m_neighbours[currentNh].setNeighbourNum(i);
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

  printNeighbours("Refresh neighbours>");

  csdebug() << "POOL SYNCHRONIZER> Neighbours saved count is: " << m_neighbours.size();
}

bool cs::PoolSynchronizer::isLastRequest() {
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
  m_neededSequences.clear();
  m_neighbours.clear();

  cslog() << "POOL SYNCHRONIZER> !!! !!! !!! !!! SYNCHRO FINISHED !!! !!! !!! !!!";
}

void cs::PoolSynchronizer::printNeighbours(const std::string& func) {
  for (const auto& neighbour : m_neighbours) {
    ConnectionPtr target = m_transport->getNeighbourByNumber(neighbour.neighbourNum());

    csdebug() << "POOL SYNCHRONIZER> " << func << " to neighbour: " << target->getOut() << ", " << neighbour;
  }
}
