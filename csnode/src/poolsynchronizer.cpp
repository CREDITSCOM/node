#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/utils.hpp>

#include <csnode/conveyer.hpp>

#include <net/transport.hpp>

cs::PoolSynchronizer::PoolSynchronizer(const PoolSyncData& data, Transport* transport, BlockChain* blockChain)
: m_maxBlockPoolsCount(data.blockPoolsCount)
, m_requestRepeatRoundCount(data.requestRepeatRoundCount)
, m_neighbourPacketsCount(data.neighbourPacketsCount)
, m_transport(transport)
, m_blockChain(blockChain)
, m_isBigBand(false) {
  m_neededSequences.reserve(m_maxBlockPoolsCount);
  m_neighbours.reserve(m_transport->getMaxNeighbours());

  refreshNeighbours();

  cslog() << "POOL SYNCHRONIZER> Pool sync data : Block pools: " << cs::numeric_cast<int>(m_maxBlockPoolsCount)
          << ", Request round: " << cs::numeric_cast<int>(m_requestRepeatRoundCount)
          << ", Neighbour packets: " << cs::numeric_cast<int>(m_neighbourPacketsCount);
}

void cs::PoolSynchronizer::processingSync(const cs::RoundNumber roundNum, bool isBigBand) {
  if (m_transport->getNeighboursCount() == 0) {
    cslog() << "POOL SYNCHRONIZER> Cannot start sync (no neighbours). Needed sequence: " << roundNum
            << ",   Requested pools block size:" << m_maxBlockPoolsCount;
    return;
  }

  const auto lastWrittenSequence = m_blockChain->getLastWrittenSequence();
  const auto roundToSync = m_isSyncroStarted ? cs::Conveyer::instance().currentRoundNumber() - 1 : roundNum;

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

      sendBlockRequest();
    }
  }
  else if (m_requestRepeatRoundCount > 0) {
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

      m_blockChain->storeBlock(pool);
      lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());
    }

    lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

    csdebug() << "POOL SYNCHRONIZER> Last written sequence on blockchain: " << lastWrittenSequence
              << ", needed seq: " << cs::Conveyer::instance().currentRoundNumber() - 1;
    showSyncronizationProgress(lastWrittenSequence);
  }
  else {
    csdebug() << "POOL SYNCHRONIZER> received sequences already contained in blockchain";
    return;
  }

  /// or m_roundToSync > lastWrittenSequence
  if (cs::Conveyer::instance().currentRoundNumber() > cs::numeric_cast<cs::RoundNumber>(lastWrittenSequence) + 1) {
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

    if (!isLastReq) {
      isLastReq = isLastRequest();
    }

    const auto firstNeededSequence = m_neededSequences.front();

    if (isLastReq) {
      csdebug() << "POOL SYNCHRONIZER> Is last neeed sequnces from: " << firstNeededSequence << ", to"
                << m_neededSequences.back();
    }

    if (isLastReq || neighbour.isAvailableSequence() || isAvailableRequest(neighbour)) {
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
  const csdb::Pool::sequence_t globalSequence =
      cs::numeric_cast<csdb::Pool::sequence_t>(cs::Conveyer::instance().currentRoundNumber());

  if (!globalSequence) {
    return;
  }

  const auto last = float(lastWrittenSequence + m_blockChain->getCachedBlocksSize());
  const auto global = float(globalSequence - 1);
  const float maxValue = 100.0f;
  const uint32_t syncStatus = cs::numeric_cast<uint32_t>(std::min(((last / global) * maxValue), maxValue));

  ProgressBar bar;
  cslog() << "SYNC: " << bar.string(syncStatus);
}

bool cs::PoolSynchronizer::checkActivity() {
  csdebug() << "POOL SYNCHRONIZER> Check activity";

  bool isNeedRequest = false;
  refreshNeighbours();

  for (auto& neighbour : m_neighbours) {
    neighbour.increaseRoundCounter();
    if (neighbour.isAvailableSequence() || isAvailableRequest(neighbour)) {
      isNeedRequest = true;
    }
    ConnectionPtr target = m_transport->getNeighbourByNumber(neighbour.neighbourNum());
    csdebug() << "POOL SYNCHRONIZER> Check activity neighbour nh: " << target->getOut()
              << ", num: " << cs::numeric_cast<int>(neighbour.neighbourNum()) << ", seq: " << neighbour.sequence()
              << ", round counter: " << neighbour.roundCounter();
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

  for (auto& el : m_requestedSequences) {
    csdebug() << "POOL SYNCHRONIZER> Storage requested sequences seq: " << el.first << ", packet: " << el.second;
  }

  emit sendRequest(target, m_neededSequences, packet);
}

bool cs::PoolSynchronizer::getNeededSequences(uint8_t nieghbourNumber) {
  const std::vector<BlockChain::SequenceInterval> requiredBlocks = m_blockChain->getRequiredBlocks();
  for (const auto& [requiredBlockBegin, requiredBlockEnd] : requiredBlocks) {
    csdebug() << "POOL SYNCHRONIZER> Required blocks begin: " << requiredBlockBegin << ", end: " << requiredBlockEnd;
  }

  const csdb::Pool::sequence_t lastWrittenSequence =
      cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

  if (!m_requestedSequences.empty()) {
    // remove unnecessary sequnces
    m_requestedSequences.erase(m_requestedSequences.begin(), m_requestedSequences.upper_bound(lastWrittenSequence));

    m_requestedSequences.erase(m_requestedSequences.begin(), m_requestedSequences.upper_bound(m_blockChain->getLastWrittenSequence()));

    std::ostringstream os;
    os << "size: " << m_requestedSequences.size() << ", el: ";
    for (const auto& [sequence, packetCounter] : m_requestedSequences) {
      csunused(packetCounter);
      os << sequence << ", ";
    }
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Requested storage : " << os.str();
  }
  else {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Requested storage: size: 0";
  }

  csdb::Pool::sequence_t sequence = lastWrittenSequence;
  bool isFromStorage = false;
  auto isNeededHelpIt =
      std::find_if(m_requestedSequences.begin(), m_requestedSequences.end(), [this](const auto& pair) {
        return ((m_neighbourPacketsCount > 0) ? (pair.second >= m_neighbourPacketsCount) : false);
      });
  auto& nh = m_neighbours[nieghbourNumber];
  const bool isRepeatRequest = isAvailableRequest(nh);
  if (isRepeatRequest) {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Neighbour: " << cs::numeric_cast<int>(nieghbourNumber)
              << " needs in reRequest";
  }

  // if storage requested sequences is impty
  if (m_requestedSequences.empty()) {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: From blockchain";
    sequence = lastWrittenSequence;
  }
  else if (isNeededHelpIt != m_requestedSequences.end()) {
    // if sequence requested is needing help
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: From requested sequences begin";
    sequence = isNeededHelpIt->first;

    if (nh.sequence() != sequence) {
      auto oldSeqIt = m_requestedSequences.find(nh.sequence());
      for (std::size_t i = 0; i < m_maxBlockPoolsCount; ++i) {
        if (oldSeqIt != m_requestedSequences.end()) {
          csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Remove exta seq: " << oldSeqIt->first;
          oldSeqIt = m_requestedSequences.erase(oldSeqIt);
        }
      }
      nh.reset();
    }

    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Sequence: " << sequence << " requested is needing help";
    isFromStorage = true;
  }
  else if (isRepeatRequest) {
    // if maxWaitingTimeReply <= 0
    sequence = std::max(nh.sequence(), lastWrittenSequence);
    isNeededHelpIt = m_requestedSequences.find(sequence);
    isFromStorage = isNeededHelpIt != m_requestedSequences.end();
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: From neighbours: "
              << m_transport->getNeighbourByNumber(nh.neighbourNum())->getOut() << ", seq: " << sequence;

    if (!isFromStorage) {
      cserror() << "POOL SYNCHRONIZER> Get needed sequences: Last sequence from heighbours is not contained in pools "
                   "requested storage";
    }
  }
  else {
    sequence = std::max(m_requestedSequences.rbegin()->first, lastWrittenSequence);
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: From requested storage: " << sequence;
  }

  csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Begin needed request Sequence: " << sequence;

  m_neededSequences.clear();

  for (std::size_t i = 0; i < m_maxBlockPoolsCount; ++i) {
    if (!isFromStorage) {
      ++sequence;
      csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Getting new equence: " << sequence;
    }

    // max sequence
    if (requiredBlocks.back().second != 0 && sequence > requiredBlocks.back().second) {
      csdebug() << "POOL SYNCHRONIZER> Check sequence interval: seq: " << sequence << ", no valid !";
      break;
    }

    for (std::size_t j = 1; j < requiredBlocks.size(); ++j) {
      // Within a valid pair
      if (sequence > requiredBlocks[j].first && sequence < requiredBlocks[j].second) {
        csdebug() << "POOL SYNCHRONIZER> Check sequence interval: seq: " << sequence << ", Within a valid pair: " << j
                  << ", " << requiredBlocks[j].first << " - " << requiredBlocks[j].second;
        break;
      }
      // Between pairs
      if (sequence > requiredBlocks[j - 1].second && sequence < requiredBlocks[j].first) {
        sequence = requiredBlocks[j].first;
        csdebug() << "POOL SYNCHRONIZER> Check sequence interval: " << j - 1
                  << ", begin: " << requiredBlocks[j - 1].first << ", end: " << requiredBlocks[j - 1].second;
        csdebug() << "POOL SYNCHRONIZER> Check sequence interval: New sequence: " << sequence
                  << ", is in the new interval: " << j << ", " << requiredBlocks[j].first << " - "
                  << requiredBlocks[j].second;
        if (isFromStorage) {
          isNeededHelpIt = m_requestedSequences.find(sequence);  // next sequence
          isFromStorage = isNeededHelpIt != m_requestedSequences.end();
          csdebug() << "POOL SYNCHRONIZER> Check sequence interval: Now Is from storage : " << isFromStorage;
        }
        break;
      }
    }

    m_neededSequences.push_back(cs::numeric_cast<cs::RoundNumber>(sequence));

    if (isFromStorage) {
      ++isNeededHelpIt;  // next sequence
      if (isNeededHelpIt == m_requestedSequences.end()) {
        break;
      }

      sequence = isNeededHelpIt->first;
      csdebug() << "POOL SYNCHRONIZER> Get needed sequences: Getting new equence is from storage: " << sequence;
    }
  }

  if (!m_neededSequences.empty()) {
    csdebug() << "POOL SYNCHRONIZER> Get needed sequences: NeededSequences: from: " << m_neededSequences.front()
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
              << ", num: " << cs::numeric_cast<int>(neighbour.neighbourNum()) << ", seq: " << neighbour.sequence()
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
      if (neighbour && !neighbour->isSignal && neighbour->lastSeq /* > m_blockChain->getLastWrittenSequence()*/) {
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
      it->second = m_neighbourPacketsCount;
    }
    m_neighbours.pop_back();
  }

  for (const auto& neighbour : m_neighbours) {
    ConnectionPtr target = m_transport->getNeighbourByNumber(neighbour.neighbourNum());
    csdebug() << "POOL SYNCHRONIZER> Refresh neighbours> neighbours: " << target->getOut()
              << ", num: " << cs::numeric_cast<int>(neighbour.neighbourNum()) << ", seq: " << neighbour.sequence()
              << ", round counter: " << neighbour.roundCounter();
  }

  csdebug() << "POOL SYNCHRONIZER> Neighbours saved count is: " << m_neighbours.size();
}

bool cs::PoolSynchronizer::isLastRequest() {
  auto sum = cs::Conveyer::instance().currentRoundNumber() - m_blockChain->getLastWrittenSequence() -
             cs::numeric_cast<uint32_t>(m_blockChain->getCachedBlocksSize());
  return sum <= m_maxBlockPoolsCount;
}

bool cs::PoolSynchronizer::isAvailableRequest(const cs::PoolSynchronizer::NeighboursSetElemet& nh) const {
  return nh.roundCounter() >= m_requestRepeatRoundCount;
}

void cs::PoolSynchronizer::synchroFinished() {
  m_isSyncroStarted = false;
  m_isBigBand = false;
  m_requestedSequences.clear();
  m_neededSequences.clear();
  m_neighbours.clear();

  cslog() << "POOL SYNCHRONIZER> !!! !!! !!! !!! SYNCHRO FINISHED !!! !!! !!! !!!";
}
