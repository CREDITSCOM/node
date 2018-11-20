#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <lib/system/progressbar.hpp>

#include <net/transport.hpp>

cs::PoolSynchronizer::PoolSynchronizer(Transport* transport, BlockChain* blockChain) :
    m_transport(transport), /// TODO Fix me. Think about how to do without it
    m_blockChain(blockChain),
    m_maxWaitingTimeReply(cs::numeric_cast<int>(m_transport->getMaxNeighbours() - 1))
{
    m_neededSequences.reserve(m_maxBlockCount);

    refreshNeighbours();
}

void cs::PoolSynchronizer::processingSync(const cs::RoundNumber roundNum) {
    if (!m_transport->isPingDone()) {
        return;
    }

    if (!m_isSyncroStarted) {
        if (roundNum >= m_blockChain->getLastWrittenSequence() + s_roundDifferent) {
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

void cs::PoolSynchronizer::getBlockReply(cs::PoolsBlock&& poolsBlock) {
    cslog() << "POOL SYNCHRONIZER> Get Block Reply <<<<<<< from: " << poolsBlock.front().sequence() << " to: " << poolsBlock.back().sequence() ;

    /// TODO Fix numeric cast from RoundNum to csdb::Pool::sequence_t
    const csdb::Pool::sequence_t writtenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());
    csdb::Pool::sequence_t lastWrittenSequence = writtenSequence;

    if (poolsBlock.back().sequence() > writtenSequence) {

        checkNeighbours(poolsBlock.front().sequence());

        for (auto& pool : poolsBlock) {
            const auto sequence = pool.sequence();
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

        csdebug() << "POOL SYNCHRONIZER> Last written sequence on blockchain: " << lastWrittenSequence << ", needed seq: " << m_roundToSync;
        if (writtenSequence != lastWrittenSequence) {
            showSyncronizationProgress(lastWrittenSequence);
        }
    }

    // Decreases, soon as a response is received for another requested block.
    const bool alreadyRequest = checkActivity();

    /// or m_roundToSync > lastWrittenSequence
    if (m_roundToSync != cs::numeric_cast<cs::RoundNumber>(lastWrittenSequence)) {
        if (!alreadyRequest) {
            sendBlockRequest();
        }
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

    if (m_neighbours.empty()) {
        csdebug() << "POOL SYNCHRONIZER> No more free requestees";
        return;
    }

    if (!getNeededSequences()) {
        csdebug() << "POOL SYNCHRONIZER> >>> All sequences already requested";
        return;
    }

    checkNeighbours(m_neededSequences.front());
    //            csdebug() << "POOL SYNCHRONIZER> target is already requested " << neighbour.c->getOut();

    cslog() << "POOL SYNCHRONIZER> Send Block Request";

    for (auto& neighbour : m_neighbours) {
        if (neighbour.sequnce == 0) {
            sendBlock(neighbour.connection, m_neededSequences);

            if (!getNeededSequences()) {
                csdebug() << "POOL SYNCHRONIZER> >>> All sequences already requested";
                break;
            }
        }
    }
}

bool cs::PoolSynchronizer::isSyncroStarted() const {
    return m_isSyncroStarted;
}

void cs::PoolSynchronizer::showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence) {
    const csdb::Pool::sequence_t globalSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_roundToSync);

    if (!globalSequence) {
      return;
    }

    const auto last = float(lastWrittenSequence);
    const auto global = float(globalSequence);
    const float maxValue = 100.0f;
    const uint32_t syncStatus = cs::numeric_cast<uint32_t>((1.0f - (global - last) / global) * maxValue);

    if (syncStatus <= maxValue) {
        ProgressBar bar;
        cslog() << "SYNC: " << bar.string(syncStatus);
    }
}

//
// Service
//

bool cs::PoolSynchronizer::checkActivity() {
    csdebug() << "POOL SYNCHRONIZER> Check activity";

    bool isNeedRequest = false;

    for (auto& [sequence, timeWaiting] : m_requestedSequences) {
        --timeWaiting;
        if (!isNeedRequest && timeWaiting <= 0) {
            isNeedRequest = true;
        }
    }

    if (isNeedRequest) {
        sendBlockRequest();
    }

    return isNeedRequest;
}

void cs::PoolSynchronizer::sendBlock(const ConnectionPtr& target, const PoolsRequestedSequences& sequences) {
    csdebug() << "POOL SYNCHRONIZER> Sending block request : from nbr: " << target->getOut() << ", id: " << target->id;
    cslog() << "POOL SYNCHRONIZER> Sending block request >>>> sequences from: " << sequences.front() << " to: " << sequences.back();

    for (const auto& sequence : sequences) {
        if (!m_requestedSequences.count(sequence)) {
            m_requestedSequences.emplace(std::make_pair(sequence, m_maxWaitingTimeReply));
        }
    }

    csdebug() << "POOL SYNCHRONIZER> Requested sequences from: " << m_requestedSequences.begin()->first << "   to: " << m_requestedSequences.rbegin()->first;

    emit sendRequest(target, sequences);
}

void cs::PoolSynchronizer::addToTemporaryStorage(const csdb::Pool& pool) {
    const auto sequence = pool.sequence();
    const auto transactionsCount = pool.transactions_count();

    if (!m_temporaryStorage.count(sequence)) {
        m_temporaryStorage.emplace(std::make_pair(sequence, pool));
        csdebug() << "POOL SYNCHRONIZER> Store received block: " << sequence << ", transactions: " << transactionsCount;
    }
}

csdb::Pool::sequence_t cs::PoolSynchronizer::processingTemporaryStorage() {
    csdb::Pool::sequence_t lastSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

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
            csdebug() << "POOL SYNCHRONIZER> Temporary storage contains sequence: " << neededSequence << ", with transactions: " << it->second.transactions_count();
            m_blockChain->onBlockReceived(it->second);
            m_temporaryStorage.erase(it);
            lastSequence = neededSequence;
        }
        else {
            csdebug() << "POOL SYNCHRONIZER> Processing TemporaryStorage: needed sequence: " << neededSequence << " not contained in storage";
            break;
        }
    }

    return lastSequence;
}

bool cs::PoolSynchronizer::getNeededSequences() {
    uint32_t lastSequence = 0;
    bool isFromStorage = false;
    auto firstSequenceIt = std::find_if(m_requestedSequences.begin(), m_requestedSequences.end(), [](const auto& pair) {
        return pair.second == 0;
    });

    if (!m_temporaryStorage.empty()) {
        csdebug() << "POOL SYNCHRONIZER> Temporary Storage begin: " << m_temporaryStorage.begin()->first << ", end: " << m_temporaryStorage.rbegin()->first;
    }

    // if storage requested sequences is impty
    if (m_requestedSequences.empty()) {
        lastSequence = m_blockChain->getLastWrittenSequence();
    }
    else if (firstSequenceIt != m_requestedSequences.end()) {
        // if maxWaitingTimeReply <= 0
        lastSequence = cs::numeric_cast<uint32_t>(firstSequenceIt->first);
        isFromStorage = true;
    }
    else {
        const uint32_t lastSeqFromRequested = cs::numeric_cast<uint32_t>(m_requestedSequences.rbegin()->first);
        const uint32_t lastSeqFromStorage = m_temporaryStorage.empty() ? 0 : cs::numeric_cast<uint32_t>(m_temporaryStorage.rbegin()->first);
        lastSequence = std::max(lastSeqFromRequested, lastSeqFromStorage);
    }

    m_neededSequences.clear();

    for (std::size_t i = 0; i < m_maxBlockCount; ++i) {
        if (!isFromStorage) {
            ++lastSequence;
        }

        // max sequence
        if (lastSequence > m_roundToSync) {
            break;
        }

        m_neededSequences.push_back(lastSequence);

        if (isFromStorage) {
            firstSequenceIt->second = m_maxWaitingTimeReply; // reset maxWaitingTimeReply
            ++firstSequenceIt; // next sequence
            if (firstSequenceIt == m_requestedSequences.end()) {
                break;
            }
            lastSequence = cs::numeric_cast<uint32_t>(firstSequenceIt->first);
        }
    }

    return !m_neededSequences.empty();
}

void cs::PoolSynchronizer::checkNeighbours(const csdb::Pool::sequence_t sequence) {
    m_neighbours.erase(NeighboursSetElemet(sequence));
}

void cs::PoolSynchronizer::refreshNeighbours() {
  const uint32_t neighboursCount = m_transport->getNeighboursCount();
  csdebug() << "POOL SYNCHRONIZER> Neighbours count from nh is: " << neighboursCount;

  if (neighboursCount == m_neighbours.size() + 1) { // + Signal
      return;
  }

  m_neighbours.clear();

  for (std::size_t i = 0; i != neighboursCount; ++i) {
      ConnectionPtr target = m_transport->getNeighbourByNumber(i);
      if (target && !target->isSignal) {
          m_neighbours.emplace(NeighboursSetElemet(0, target));
      }
  }

  csdebug() << "POOL SYNCHRONIZER> Neighbours count is: " << m_neighbours.size();
}
