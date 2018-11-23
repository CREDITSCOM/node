#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/utils.hpp>

#include <net/transport.hpp>

cs::PoolSynchronizer::PoolSynchronizer(Transport* transport, BlockChain* blockChain)
: m_transport(transport)
, m_blockChain(blockChain) {
  m_neededSequences.reserve(s_maxBlockCount);
  m_neighbours.reserve(m_transport->getMaxNeighbours());

  refreshNeighbours();
}

void cs::PoolSynchronizer::processingSync(const cs::RoundNumber roundNum) {
  if (m_transport->getNeighboursCount() == 0) {
    cslog() << "POOL SYNCHRONIZER> Cannot start sync (no neighbours). Needed sequence: " << roundNum
            << ",   Requested pools block size:" << s_maxBlockCount;
    return;
  }

  if (!m_isSyncroStarted) {
    if (roundNum >= m_blockChain->getLastWrittenSequence() + s_roundDifferentForSync) {
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

        m_blockChain->onBlockReceived(pool);
        lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());
      }
      else if (sequence > lastWrittenSequence) {
        addToTemporaryStorage(pool);
      }
    }

    lastWrittenSequence = processingTemporaryStorage();

    csdebug() << "POOL SYNCHRONIZER> Last written sequence on blockchain: " << lastWrittenSequence
              << ", needed seq: " << m_roundToSync;
    showSyncronizationProgress(lastWrittenSequence);
  }
  else {
    csdebug() << "POOL SYNCHRONIZER> received sequences already contained in blockchain";
    return;
  }

  /// or m_roundToSync > lastWrittenSequence
  if (m_roundToSync != cs::numeric_cast<cs::RoundNumber>(lastWrittenSequence)) {
//    cs::Timer::singleShot(cs::NeighboursRequestDelay, [this] {
//      sendBlockRequest();
//    });
  }
  else {
    m_isSyncroStarted = false;
    m_roundToSync = 0;
    m_requestedSequences.clear();
    m_temporaryStorage.clear();
    m_neededSequences.clear();
    m_neighbours.clear();

    cslog() << "POOL SYNCHRONIZER> !!! !!! !!! !!! SYNCHRO FINISHED !!! !!! !!! !!!";
    emit synchroFinished();
  }
}

void cs::PoolSynchronizer::sendBlockRequest() {
  refreshNeighbours();

  for (const auto& neighbour : m_neighbours) {
    csdebug() << "POOL SYNCHRONIZER> nh: " << neighbour.connection()->getOut() << ", seq: " << neighbour.sequence()
              << ", round counter: " << neighbour.roundCounter();
  }
  for (const auto& sequence : m_requestedSequences) {
    csdebug() << "POOL SYNCHRONIZER> requested seq: " << sequence.first << ", pack: " << sequence.second;
  }

  if (m_neighbours.empty()) {
    csdebug() << "POOL SYNCHRONIZER> No more free requestees";
    return;
  }

  if (!getNeededSequences()) {
    csdebug() << "POOL SYNCHRONIZER> >>> All sequences already requested";
    return;
  }

  csdebug() << "POOL SYNCHRONIZER> send Block Request start";

  const auto firstNeededSequence = m_neededSequences.front();

  bool isLastReq = isLastRequest();
  if (isLastReq) {
    csdebug() << "POOL SYNCHRONIZER> Is last neeed sequnces from: " << firstNeededSequence << ", to"
              << m_neededSequences.back();
  }

  if (!isLastReq) {
    // sequence = 0 if already requested
    auto it = std::find_if(m_neighbours.begin(), m_neighbours.end(), [firstNeededSequence](const auto& neighbour) {
      return neighbour.sequence() == cs::numeric_cast<csdb::Pool::sequence_t>(firstNeededSequence);
    });

    if (it != m_neighbours.end() && it->isAvailableRequest()) {
      checkNeighbourSequence(firstNeededSequence);
    }
  }

  for (auto& neighbour : m_neighbours) {
    if (isLastReq || neighbour.isAvailableSequence()) {
      neighbour.setSequence(m_neededSequences.front());
      sendBlock(neighbour.connection());

      if (!isLastReq && !getNeededSequences()) {
        csdebug() << "POOL SYNCHRONIZER> !!! All sequences already requested";
        break;
      }
    }
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
  const uint32_t syncStatus = cs::numeric_cast<uint32_t>((last / global) * maxValue);

  if (syncStatus <= maxValue) {
    ProgressBar bar;
    cslog() << "SYNC: " << bar.string(syncStatus);
  }
}

bool cs::PoolSynchronizer::checkActivity() {
  csdebug() << "POOL SYNCHRONIZER> Check activity";

  bool isNeedRequest = false;

  for (auto& neighbour : m_neighbours) {
    if (neighbour.increaseRoundCounter() && !isNeedRequest) {
      isNeedRequest = true;
    }
    csdebug() << "POOL SYNCHRONIZER> Check activity neighbour nh: " << neighbour.connection()->getOut()
              << ", seq: " << neighbour.sequence() << ", round counter: " << neighbour.roundCounter();
  }

  if (isNeedRequest) {
    sendBlockRequest();
  }

  return isNeedRequest;
}

void cs::PoolSynchronizer::sendBlock(const ConnectionPtr& target) {
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

void cs::PoolSynchronizer::addToTemporaryStorage(const csdb::Pool& pool) {
  const auto sequence = pool.sequence();
  const auto transactionsCount = pool.transactions_count();

  if (!m_temporaryStorage.count(sequence)) {
    m_temporaryStorage.emplace(std::make_pair(sequence, pool));
    csdebug() << "POOL SYNCHRONIZER> Store received block in temporary storage: " << sequence
              << ", transactions: " << transactionsCount;
  }
}

csdb::Pool::sequence_t cs::PoolSynchronizer::processingTemporaryStorage() {
  csdb::Pool::sequence_t lastSequence =
      cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

  if (m_temporaryStorage.empty()) {
    csdebug() << "POOL SYNCHRONIZER> Temporary storage is empty";
    return lastSequence;
  }

  csdb::Pool::sequence_t neededSequence = lastSequence;
  const auto storageSize = m_temporaryStorage.size();

  for (std::size_t i = 0; i < storageSize; ++i) {
    ++neededSequence;
    csdebug() << "POOL SYNCHRONIZER> Processing TemporaryStorage: needed sequence: " << neededSequence;

    auto it = m_temporaryStorage.find(neededSequence);

    if (it != m_temporaryStorage.end()) {
      csdebug() << "POOL SYNCHRONIZER> Temporary storage contains sequence: " << neededSequence
                << ", with transactions: " << it->second.transactions_count();
      m_blockChain->onBlockReceived(it->second);
      m_temporaryStorage.erase(it);
      lastSequence = neededSequence;
    }
    else {
      csdebug() << "POOL SYNCHRONIZER> Processing TemporaryStorage sequence: " << neededSequence
                << " not contained in storage";
      break;
    }
  }

  return lastSequence;
}

bool cs::PoolSynchronizer::getNeededSequences() {
  uint32_t lastSequence = 0;
  bool isFromStorage = false;
  auto sequenceIt = std::find_if(m_requestedSequences.begin(), m_requestedSequences.end(),
                                 [](const auto& pair) { return pair.second >= s_packetCountForHelp; });
  auto neighbourIt = std::find_if(m_neighbours.begin(), m_neighbours.end(),
                                  [](const auto& neighbour) { return neighbour.isAvailableRequest() && neighbour.sequence(); });

  if (!m_temporaryStorage.empty()) {
    std::ostringstream os;
    os << "size: " << m_temporaryStorage.size() << ", el: ";
    for (const auto& [sequence, value] : m_temporaryStorage) {
      os << sequence << ", ";
    }
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences temporary storage: " << os.str();
  }
  else {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences temporary storage: size: 0";
  }

  if (!m_requestedSequences.empty()) {
    std::ostringstream os;
    os << "size: " << m_requestedSequences.size() << ", el: ";
    for (const auto& [sequence, packetCounter] : m_requestedSequences) {
      os << sequence << ", ";
    }
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences requested storage  : " << os.str();
  }
  else {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences requested storage: size: 0";
  }

  // if storage requested sequences is impty
  if (m_requestedSequences.empty()) {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from blockchain";
    lastSequence = m_blockChain->getLastWrittenSequence();
  }
  else if (sequenceIt != m_requestedSequences.end()) {
    // if m_maxPacketCounter
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from requestedSequences begin";
    lastSequence = cs::numeric_cast<uint32_t>(sequenceIt->first);
    isFromStorage = true;
  }
  else if (neighbourIt != m_neighbours.end()) {
    // if maxWaitingTimeReply <= 0
    lastSequence = cs::numeric_cast<uint32_t>(neighbourIt->sequence());
    if (!lastSequence) {

    }
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: from neighbours: " << neighbourIt->connection()->getOut()
              << ", seq: " << lastSequence;
    isFromStorage = true;
    sequenceIt = m_requestedSequences.find(lastSequence);
    if (sequenceIt == m_requestedSequences.end()) {
      cserror() << "POOL SYNCHRONIZER> Get needed sequences: last sequence from heighbours is not contained in pools "
                   "requested storage";
    }
  }
  else {
    const uint32_t lastSeqFromRequested = cs::numeric_cast<uint32_t>(m_requestedSequences.rbegin()->first);
    const uint32_t lastSeqFromStorage =
        m_temporaryStorage.empty() ? 0 : cs::numeric_cast<uint32_t>(m_temporaryStorage.rbegin()->first);
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
      ++sequenceIt;  // next sequence
      if (sequenceIt == m_requestedSequences.end()) {
        break;
      }

      lastSequence = cs::numeric_cast<uint32_t>(sequenceIt->first);
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

  for (std::size_t i = 0; i < m_neighbours.size();) {
    const bool isEqual = m_neighbours[i].isEqual(sequence);
    const auto& nh = m_neighbours[i];

    if (isEqual) {
      m_neighbours[i].reset();
      std::swap(m_neighbours[i], m_neighbours.front());
    }
    csdebug() << "POOL SYNCHRONIZER> neighbours: " << nh.connection()->getOut() << ", seq: " << nh.sequence()
              << ", round counter: " << nh.roundCounter();
    ++i;
  }
}

void cs::PoolSynchronizer::refreshNeighbours() {
  const uint32_t neededNeighboursCount = m_transport->getNeighboursCountWithoutSS();
  csdebug() << "POOL SYNCHRONIZER> Neighbours count is: " << neededNeighboursCount;

  if (neededNeighboursCount == m_neighbours.size()) {
    return;
  }

  const uint32_t neighboursCount = m_transport->getNeighboursCount();
  const auto nSize = m_neighbours.size();

  for (std::size_t i = nSize; i < neighboursCount; ++i) {
    ConnectionPtr target = m_transport->getNeighbourByNumber(i);
    if (target && !target->isSignal) {
      m_neighbours.emplace_back(NeighboursSetElemet(target));
    }
  }
  for (std::size_t i = neighboursCount; i < nSize; ++i) {
    m_neighbours.pop_back();
  }

  csdebug() << "POOL SYNCHRONIZER> Neighbours saved count is: " << m_neighbours.size();
}

bool cs::PoolSynchronizer::isLastRequest() {
  auto sum =
      m_roundToSync - m_blockChain->getLastWrittenSequence() - cs::numeric_cast<uint32_t>(m_temporaryStorage.size());
  return sum <= s_maxBlockCount;
}
