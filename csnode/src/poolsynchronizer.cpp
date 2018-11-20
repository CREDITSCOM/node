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
    m_receivedSequences.reserve(m_maxBlockCount);
}

void cs::PoolSynchronizer::processingSync(const cs::RoundNumber roundNum) {
    if (!m_isSyncroStarted) {
        if (roundNum != m_blockChain->getLastWrittenSequence() + 1) {
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

    for (auto& pool : poolsBlock) {
        const auto sequence = pool.sequence();

        m_transport->syncReplied(cs::numeric_cast<uint32_t>(sequence));

        if (m_blockChain->getGlobalSequence() < sequence) {
            m_blockChain->setGlobalSequence(cs::numeric_cast<uint32_t>(sequence));
        }

        auto it = m_requestedSequences.find(sequence);
        if (it != m_requestedSequences.end()) {
            m_requestedSequences.erase(it);
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
        m_receivedSequences.clear();

        cslog() << "POOL SYNCHRONIZER> !!! !!! !!! !!! SYNCHRO FINISHED !!! !!! !!! !!!";
        emit synchroFinished();
    }
}

void cs::PoolSynchronizer::sendBlockRequest() {
    uint32_t neighboursCount = m_transport->getNeighboursCount();

    if (m_transport->getNeighboursCount() == 0) {
        csdebug() << "POOL SYNCHRONIZER> Neighbours count is 0";
        return;
    }

    cslog() << "POOL SYNCHRONIZER> Send Block Request";

    bool isRequest = false;
    bool isRepeat = false;

    for (; neighboursCount > 0; ) {
        if (!isRepeat && !getPoolRequestedSequences()) {
            csdebug() << "POOL SYNCHRONIZER> >>> All sequences already requested";
            break;
        }

        const uint32_t sequence = cs::numeric_cast<uint32_t>(m_receivedSequences.front());

        bool alreadyRequested = false;
        ConnectionPtr target = m_transport->getSyncRequestee(sequence, alreadyRequested); /// TODO Fix me. Think, how do without it

        if (!target) {
            csdebug() << "POOL SYNCHRONIZER> No more free requestees";
            break;  // No more free requestees
        }

        isRequest = true;

        if (alreadyRequested) {  // Already requested this block from this guy?
            csdebug() << "POOL SYNCHRONIZER> target is already requested " << target->getOut();
            m_transport->syncReplied(sequence);
            isRepeat = true;
        }
        else { /// Fix me
            isRepeat = false;
            sendBlock(target, m_receivedSequences);
            --neighboursCount;
        }
    }

    if (!isRequest && m_requestedSequences.empty() && m_temporaryStorage.empty()) {
        m_isSyncroStarted = false;
        csdebug() << "POOL SYNCHRONIZER> !!! syncro started is false";
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
    cslog() << "POOL SYNCHRONIZER> Sending block request >>>> sequences from: " << sequences.front() << "   to: " << sequences.back();

    for (const auto& sequence : sequences) {
        if (!m_requestedSequences.count(sequence)) {
            m_requestedSequences.emplace(std::make_pair(sequence, m_maxWaitingTimeReply));
        }
    }

    csdebug() << "POOL SYNCHRONIZER> Storage requested sequences from: " << m_requestedSequences.begin()->first << "   to: " << m_requestedSequences.rbegin()->first;

    emit sendRequest(target, sequences);
}

void cs::PoolSynchronizer::addToTemporaryStorage(const csdb::Pool& pool) {
    const auto sequence = pool.sequence();
    const auto transactionsCount = pool.transactions_count();

    if (!m_temporaryStorage.count(sequence)) {
        m_temporaryStorage.emplace(std::make_pair(sequence, pool));
        csdebug() << "POOL SYNCHRONIZER> Store received block: " << sequence << ",      transactions: " << transactionsCount;
    }
}

csdb::Pool::sequence_t cs::PoolSynchronizer::processingTemporaryStorage() {
    csdb::Pool::sequence_t lastSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

    if (m_temporaryStorage.empty()) {
        csdebug() << "POOL SYNCHRONIZER> temaporary storage is empty";
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

bool cs::PoolSynchronizer::getPoolRequestedSequences() {
    uint32_t lastSequence = 0;
    bool isFromStorage = false;
    auto firstSequenceIt = std::find_if(m_requestedSequences.begin(), m_requestedSequences.end(), [](const auto& pair) {
        return pair.second == 0;
    });

    m_receivedSequences.clear();

    if (!m_requestedSequences.empty()) {
        csdebug() << "POOL SYNCHRONIZER> Requested sequences begin: " << m_requestedSequences.begin()->first << ", end: " << m_requestedSequences.rbegin()->first;
    }
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

    for (std::size_t i = 0; i < m_maxBlockCount; ++i) {
        if (!isFromStorage) {
            ++lastSequence;
        }

        // max sequence
        if (lastSequence > m_roundToSync) {
            break;
        }

        m_receivedSequences.push_back(lastSequence);

        if (isFromStorage) {
            firstSequenceIt->second = m_maxWaitingTimeReply; // reset maxWaitingTimeReply
            ++firstSequenceIt; // next sequence
            if (firstSequenceIt == m_requestedSequences.end()) {
                break;
            }
            lastSequence = cs::numeric_cast<uint32_t>(firstSequenceIt->first);
        }
    }

    return !m_receivedSequences.empty();
}
