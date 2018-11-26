#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/utils.hpp>

#include <net/transport.hpp>

cs::PoolSynchronizer::PoolSynchronizer(Transport* transport, BlockChain* blockChain)
: m_transport(transport)
, m_blockChain(blockChain)
, m_isBigBand(false) {
  m_neededSequences.reserve(s_maxBlockCount);
  m_neighbours.reserve(m_transport->getMaxNeighbours());

  refreshNeighbours();
}

void cs::PoolSynchronizer::processingSync(const cs::RoundNumber roundNum, bool isBigBand) {
  if (m_transport->getNeighboursCount() == 0) {
    cslog() << "POOL SYNCHRONIZER> Cannot start sync (no neighbours). Needed sequence: " << roundNum
            << ",   Requested pools block size:" << s_maxBlockCount;
    return;
  }

  const auto lastWrittenSequence = m_blockChain->getLastWrittenSequence();
  const auto roundToSync = m_isSyncroStarted ? m_roundToSync : roundNum;

  if (lastWrittenSequence >= roundToSync) {
    showSyncronizationProgress(cs::numeric_cast<csdb::Pool::sequence_t>(lastWrittenSequence));
    synchroFinished();
    return;
  }

  m_isBigBand = isBigBand;

  if (!m_isSyncroStarted) {
    if (roundNum >= lastWrittenSequence + s_roundDifferentForSync) {
      cslog() << "POOL SYNCHRONIZER> Processing Pools Sync Start. Needed sequence: " << roundNum;
      m_isSyncroStarted = true;
      m_roundToSync = roundNum;

      sendBlockRequest();
    }
  }
  else {
    checkActivity();
  }
}

void cs::PoolSynchronizer::getBlockReply(cs::PoolsBlock&& poolsBlock, uint32_t packet) {
  cslog() << "POOL SYNCHRONIZER> Get Block Reply <<<<<<< from: " << poolsBlock.front().sequence()
          << " to: " << poolsBlock.back().sequence() << ",   packet: " << packet;

  /// TODO Fix numeric cast from RoundNum to csdb::Pool::sequence_t
  csdb::Pool::sequence_t lastWrittenSequence =
      cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

  if (poolsBlock.back().sequence() > lastWrittenSequence) {
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

      if (sequence == lastWrittenSequence + 1) {
        cslog() << "POOL SYNCHRONIZER> Block Sequence is Ok " << sequence;
      }
      else if (sequence > lastWrittenSequence) {
        csdebug() << "POOL SYNCHRONIZER> Store received block in temporary storage: " << pool.sequence()
                  << ", transactions: " << pool.transactions_count();
        m_temporaryStorage.emplace(pool.sequence());
      }

      m_blockChain->storeBlock(pool);
      lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());
    }

    lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

    csdebug() << "POOL SYNCHRONIZER> Last written sequence on blockchain: " << lastWrittenSequence
              << ", needed seq: " << m_roundToSync;
    showSyncronizationProgress(lastWrittenSequence);
  }
  else {
    csdebug() << "POOL SYNCHRONIZER> received sequences already contained in blockchain";
    return;
  }

  /// or m_roundToSync > lastWrittenSequence
  if (m_roundToSync > cs::numeric_cast<cs::RoundNumber>(lastWrittenSequence)) {
    if(m_isBigBand) {
      cs::Timer::singleShot(cs::NeighboursRequestDelay, [this] {
        sendBlockRequest();
      });
    }
  }
  else {
    synchroFinished();
  }
}

void cs::PoolSynchronizer::sendBlockRequest() {
  refreshNeighbours();

  for (const auto& sequence : m_requestedSequences) {
    csdebug() << "POOL SYNCHRONIZER> requested seq: " << sequence.first << ", pack: " << sequence.second;
  }

  if (m_neighbours.empty()) {
    csdebug() << "POOL SYNCHRONIZER> No more free requestees";
    return;
  }

  csdebug() << "POOL SYNCHRONIZER> send Block Request start";
  bool isLastReq = false;

  std::uint8_t idx = 0;
  for (auto& neighbour : m_neighbours) {
    if (!isLastReq && !getNeededSequences(idx++)) {
      csdebug() << "POOL SYNCHRONIZER> >>> All sequences already requested";
      continue;
    }

    isLastReq = isLastRequest();

    const auto firstNeededSequence = m_neededSequences.front();

    if (isLastReq) {
      csdebug() << "POOL SYNCHRONIZER> Is last neeed sequnces from: " << firstNeededSequence << ", to"
                << m_neededSequences.back();
    }

    if (isLastReq || neighbour.isAvailableSequence() || neighbour.isAvailableRequest()) {
      neighbour.setSequence(firstNeededSequence);
      sendBlock(neighbour.neighbourNum());
    }
  }

  if (m_neededSequences.empty()) {
    csdebug() << "POOL SYNCHRONIZER> >>> All sequences already requested";
  }
}

bool cs::PoolSynchronizer::isSyncroStarted() const {
  return m_isSyncroStarted;
}

//
// Service
//

void cs::PoolSynchronizer::showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence) {
  const csdb::Pool::sequence_t globalSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_roundToSync);

  if (!globalSequence) {
    return;
  }

  const auto last = float(lastWrittenSequence + m_temporaryStorage.size());
  const auto global = float(globalSequence);
  const float maxValue = 100.0f;
  uint32_t syncStatus = cs::numeric_cast<uint32_t>(std::min(((last / global) * maxValue), maxValue));

  ProgressBar bar;
  cslog() << "SYNC: " << bar.string(syncStatus);
}

bool cs::PoolSynchronizer::checkActivity() {
  csdebug() << "POOL SYNCHRONIZER> Check activity";

  bool isNeedRequest = false;
  refreshNeighbours();

  for (auto& neighbour : m_neighbours) {
    if (neighbour.isAvailableSequence() || neighbour.increaseRoundCounter()) {
      isNeedRequest = true;
    }
    ConnectionPtr target = m_transport->getNeighbourByNumber(neighbour.neighbourNum());
    csdebug() << "POOL SYNCHRONIZER> Check activity neighbour nh: " << target->getOut()
              << ", num: " << cs::numeric_cast<int>(neighbour.neighbourNum())
              << ", seq: " << neighbour.sequence() << ", round counter: " << neighbour.roundCounter();
  }
  csdebug() << "POOL SYNCHRONIZER> Check activity, repeat request: " << isNeedRequest;

  if (isNeedRequest) {
    sendBlockRequest();
  }

  return isNeedRequest;
}

void cs::PoolSynchronizer::sendBlock(uint8_t neighbourNum) {
  ConnectionPtr target = m_transport->getNeighbourByNumber(neighbourNum);

  if (!target) {
    cserror() << "POOL SYNCHRONIZER> Sending block request : Target is not valid";
    return;
  }

  csdebug() << "POOL SYNCHRONIZER> Sending block request : from nbr: " << target->getOut();

  uint32_t packet = 0;

  for (const auto& sequence : m_neededSequences) {
    if (!m_requestedSequences.count(sequence)) {
      m_requestedSequences.emplace(std::make_pair(sequence, 0));
    }
    packet = ++(m_requestedSequences.at(sequence));
  }

  cslog() << "POOL SYNCHRONIZER> Sending block request >>>>>>> needed seq: "
          << m_blockChain->getLastWrittenSequence() + 1
          << ", requested block sequences from: " << m_neededSequences.front() << ", to: " << m_neededSequences.back()
          << ",   packet: " << packet;

  //  csdebug() << "POOL SYNCHRONIZER> Storage requested sequences from: " << m_requestedSequences.begin()->first
  //            << ", to: " << m_requestedSequences.rbegin()->first;

  for (auto& el : m_requestedSequences) {
    csdebug() << "POOL SYNCHRONIZER> Storage requested sequences seq: " << el.first << ", packet: " << el.second;
  }

  emit sendRequest(target, m_neededSequences, packet);
}

bool cs::PoolSynchronizer::getNeededSequences(uint8_t nieghbourNumber) {
  auto upperRequestedIt = m_requestedSequences.upper_bound(m_blockChain->getLastWrittenSequence());
  if (upperRequestedIt != m_requestedSequences.end()) {
    m_requestedSequences.erase(m_requestedSequences.begin(), upperRequestedIt);
  }
  auto upperTempIt = m_temporaryStorage.upper_bound(m_blockChain->getLastWrittenSequence());
  if (upperTempIt != m_temporaryStorage.end()) {
    m_temporaryStorage.erase(m_temporaryStorage.begin(), upperTempIt);
  }

  if (!m_temporaryStorage.empty()) {
    if (m_blockChain->getLastWrittenSequence() > *m_temporaryStorage.rbegin()) {
      m_temporaryStorage.clear();
    }

    std::ostringstream os;
    os << "size: " << m_temporaryStorage.size() << ", el: ";

    for (const auto& sequence : m_temporaryStorage) {
      os << sequence << ", ";
    }

    csdebug() << "POOL SYNCHRONIZER> Get needed sequences temporary storage: " << os.str();
  }
  else {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences temporary storage: size: 0";
  }

  if (!m_requestedSequences.empty()) {
    if (m_blockChain->getLastWrittenSequence() > m_requestedSequences.rbegin()->first) {
      m_requestedSequences.clear();
    }

    std::ostringstream os;
    os << "size: " << m_requestedSequences.size() << ", el: ";
    for (const auto& [sequence, packetCounter] : m_requestedSequences) {
      csunused(packetCounter);
      os << sequence << ", ";
    }
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences requested storage  : " << os.str();
  }
  else {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences requested storage: size: 0";
  }

  uint32_t lastSequence = 0;
  bool isFromStorage = false;
  auto isNeededHelpIt = std::find_if(m_requestedSequences.begin(), m_requestedSequences.end(),
                                 [](const auto& pair) { return pair.second >= s_packetCountForHelp; });
  auto& nh = m_neighbours[nieghbourNumber];
  const bool isRepeatRequest = nh.isAvailableRequest();

  // if storage requested sequences is impty
  if (m_requestedSequences.empty()) {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from blockchain";
    lastSequence = m_blockChain->getLastWrittenSequence();
  }
  else if (isNeededHelpIt != m_requestedSequences.end()) {
    // if sequence requested is needing help
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from requestedSequences begin";
    lastSequence = cs::numeric_cast<uint32_t>(isNeededHelpIt->first);

    auto oldSeqIt = m_requestedSequences.find(nh.sequence());
    if (oldSeqIt != m_requestedSequences.end()) {
      oldSeqIt->second = s_packetCountForHelp;
    }
    nh.reset();
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: sequence: " << lastSequence << " requested is needing help";
    isFromStorage = true;
  }
  else if (isRepeatRequest) {
    // if maxWaitingTimeReply <= 0
    lastSequence = std::max(cs::numeric_cast<uint32_t>(nh.sequence()), m_blockChain->getLastWrittenSequence());
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from neighbours: "
              << m_transport->getNeighbourByNumber(nh.neighbourNum())->getOut()
              << ", seq: " << lastSequence;
    isFromStorage = true;
    isNeededHelpIt = m_requestedSequences.find(lastSequence);
    if (isNeededHelpIt == m_requestedSequences.end()) {
      cserror() << "POOL SYNCHRONIZER> Get needed sequences: last sequence from heighbours is not contained in pools "
                   "requested storage";
    }
  }
  else {
    const uint32_t lastSeqFromRequested =
        std::max(cs::numeric_cast<uint32_t>(m_requestedSequences.rbegin()->first), m_blockChain->getLastWrittenSequence());
    const uint32_t lastSeqFromStorage =
        m_temporaryStorage.empty() ? 0 : cs::numeric_cast<uint32_t>(*m_temporaryStorage.rbegin());
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from requested storage: " << lastSeqFromRequested;
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from temp storage: " << lastSeqFromStorage;
    lastSequence = std::max(lastSeqFromRequested, lastSeqFromStorage);
  }
  csdebug() << "POOL SYNCHRONIZER> Get needed sequences: begin needed request Sequence: " << lastSequence;

  m_neededSequences.clear();

  for (std::size_t i = 0; i < s_maxBlockCount; ++i) {
    if (!isFromStorage) {
      ++lastSequence;
    }

    // max sequence
    if (lastSequence > m_roundToSync) {
      break;
    }

    m_neededSequences.push_back(lastSequence);

    if (isFromStorage) {
      ++isNeededHelpIt;  // next sequence
      if (isNeededHelpIt == m_requestedSequences.end()) {
        break;
      }

      lastSequence = cs::numeric_cast<uint32_t>(isNeededHelpIt->first);
    }
  }

  if (!m_neededSequences.empty()) {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: neededSequences: from: " << m_neededSequences.front()
              << ", to: " << m_neededSequences.back();
  }

  return !m_neededSequences.empty();
}

void cs::PoolSynchronizer::checkNeighbourSequence(const csdb::Pool::sequence_t sequence) {
  csdebug() << "POOL SYNCHRONIZER> Check neighbour sequence: " << sequence;

  for (std::size_t i = 0; i < m_neighbours.size(); ++i) {
    const bool isEqual = m_neighbours[i].isEqual(sequence);
    auto& nh = m_neighbours[i];

    if (isEqual) {
      nh.reset();
      std::swap(nh, m_neighbours.front());
    }
  }

  for (const auto& neighbour : m_neighbours) {
    ConnectionPtr target = m_transport->getNeighbourByNumber(neighbour.neighbourNum());
    csdebug() << "POOL SYNCHRONIZER> neighbours: " << target->getOut()
              << ", num: " << cs::numeric_cast<int>(neighbour.neighbourNum())
              << ", seq: " << neighbour.sequence()
              << ", round counter: " << neighbour.roundCounter();
  }
}

void cs::PoolSynchronizer::refreshNeighbours() {
  const uint32_t neededNeighboursCount = m_transport->getNeighboursCountWithoutSS();
  auto nSize = m_neighbours.size();

  csdebug() << "POOL SYNCHRONIZER> Neighbours count is: " << neededNeighboursCount;

  if (nSize == neededNeighboursCount) {
    return;
  }

  const uint32_t allNeighboursCount = m_transport->getNeighboursCount();

  // Add new neighbours
  if (nSize < neededNeighboursCount) {
    if (nSize > 0) {
      ++nSize;
    }
    for (uint8_t i = cs::numeric_cast<uint8_t>(nSize); i < cs::numeric_cast<uint8_t>(allNeighboursCount); ++i) {
      ConnectionPtr neighbour = m_transport->getNeighbourByNumber(i);
      if (neighbour && !neighbour->isSignal && neighbour->lastSeq/* > m_blockChain->getLastWrittenSequence()*/) {
        m_neighbours.emplace_back(NeighboursSetElemet(i));
      }
    }
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
    auto it = m_requestedSequences.find(m_neighbours.back().sequence());
    if (it != m_requestedSequences.end()) {
      it->second = s_packetCountForHelp;
    }
    m_neighbours.pop_back();
  }

  for (const auto& neighbour : m_neighbours) {
    ConnectionPtr target = m_transport->getNeighbourByNumber(neighbour.neighbourNum());
    csdebug() << "POOL SYNCHRONIZER> Refresh neighbours> neighbours: " << target->getOut()
              << ", num: " << cs::numeric_cast<int>(neighbour.neighbourNum())
              << ", seq: " << neighbour.sequence()
              << ", round counter: " << neighbour.roundCounter();
  }

  csdebug() << "POOL SYNCHRONIZER> Neighbours saved count is: " << m_neighbours.size();
}

bool cs::PoolSynchronizer::isLastRequest() {
  auto sum =
      m_roundToSync - m_blockChain->getLastWrittenSequence() - cs::numeric_cast<uint32_t>(m_temporaryStorage.size());
  return sum <= s_maxBlockCount;
}

void cs::PoolSynchronizer::synchroFinished() {
  m_isSyncroStarted = false;
  m_roundToSync = 0;
  m_requestedSequences.clear();
  m_temporaryStorage.clear();
  m_neededSequences.clear();
  m_neighbours.clear();

  cslog() << "POOL SYNCHRONIZER> !!! !!! !!! !!! SYNCHRO FINISHED !!! !!! !!! !!!";
}
