#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <lib/system/progressbar.hpp>

#include <net/transport.hpp>

cs::PoolSynchronizer::PoolSynchronizer(Transport* transport, BlockChain* blockChain) :
    m_transport(transport), /// TODO Fix me. Think about how to do without it
    m_blockChain(blockChain),
    m_maxWaitingTimeReply(cs::numeric_cast<int>(m_transport->getNeighboursCount() * 3))
{
    m_receivedSequences.reserve(m_maxBlockCount);
#ifdef USE_REQUEST_TIMER
    cs::Connector::connect(&m_timer.timeOut, this, &cs::PoolSynchronizer::reSendBlockRequestToRandomNeighbour);
#endif
}

void cs::PoolSynchronizer::processingSync(const cs::RoundNumber roundNum) {
  cslog() << "POOL SYNCHRONIZER> Processing pools sync ....";
    if (!m_isSyncroStarted) {
        if (roundNum != m_blockChain->getLastWrittenSequence() + 1) {
            m_isSyncroStarted = true;
            m_roundToSync = roundNum;

            cslog() << "POOL SYNCHRONIZER> Processing pools sync Start";
            sendBlockRequest();
        }
    }
}

void cs::PoolSynchronizer::getBlockReply(cs::PoolsBlock&& poolsBlock) {
    csdebug() << "POOL SYNCHRONIZER> Get Block Reply from " << poolsBlock.front().sequence() << ", to" << poolsBlock.back().sequence() ;

#ifdef USE_REQUEST_TIMER
    if (m_timer.isRunning()) {
        m_timer.stop();
    }
#endif

    /// TODO Fix numeric cast from RoundNum to csdb::Pool::sequence_t
    csdb::Pool::sequence_t lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

    for (auto& pool : poolsBlock) {
        const auto sequence = pool.sequence();

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
            csdebug() << "POOL SYNCHRONIZER> Get block reply> GETTER lastWrittenSequence " << lastWrittenSequence;
        }
        else {
            addToTemporaryStorage(pool);
        }
    }

    processingTemporaryStorage();
    lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

    csdebug() << "POOL SYNCHRONIZER> Get block reply> GETTER lastWrittenSequence " << lastWrittenSequence;
    showSyncronizationProgress(lastWrittenSequence);


    // Decreases, soon as a response is received for another requested block.
    for (auto& [key, value] : m_requestedSequences) {
        --value;
    }

    csdebug() << "POOL SYNCHRONIZER> Get block reply> lastWrittenSequence " << lastWrittenSequence;
    csdebug() << "POOL SYNCHRONIZER> Get block reply> m_roundToSync " << m_roundToSync;
    csdebug() << "POOL SYNCHRONIZER> Get block reply> last requested Sequences " << m_requestedSequences.rbegin()->first;

    if (m_roundToSync != cs::numeric_cast<cs::RoundNumber>(lastWrittenSequence)) {
//        csdebug() << "POOL SYNCHRONIZER> Get block reply> lastWrittenSequence " << lastWrittenSequence;
        sendBlockRequest(false);

#ifdef USE_REQUEST_TIMER
        m_timer.start(cs::NeighboursRequestDelay * 5);
#endif
    }
    else {
        m_isSyncroStarted = false;
        m_roundToSync = 0;
        m_requestedSequences.clear();
        m_temporaryStorage.clear();

        cslog() << "POOL SYNCHRONIZER> !!! SYNCRO FINISHED !!!";
#ifdef USE_REQUEST_TIMER
        m_transport->resetNeighbours();
#endif
        emit synchroFinished();
    }
}

void cs::PoolSynchronizer::sendBlockRequest(const bool isAllRequest) {
    cslog() << "POOL SYNCHRONIZER> Send Block Request";

    uint32_t neighbours = isAllRequest ? m_transport->getNeighboursCount() : 1;

    getPoolRequestedSequences(m_receivedSequences);

    bool isRequested = false;

    while (neighbours) { /// FIX
        if (m_receivedSequences.size() == 0) {
            csdebug() << "POOL SYNCHRONIZER> Send Block Request> >>> !!!!!!!!!!!!!!!!!! sequences size is 0";
            break;
        }

        bool alreadyRequested = false;
        ConnectionPtr target = m_transport->getSyncRequestee(m_receivedSequences.front(), alreadyRequested); /// TODO Fix me. Think, how do without it
        csdebug() << "SOKOLOV> >>> neighbour num: " << neighbours;

        if (!target) {
            csdebug() << "POOL SYNCHRONIZER> No more free requestees";
            break;  // No more free requestees
        }

        isRequested = true;

        if (!alreadyRequested) {  // Already requested this block from this guy?
            sendBlock(target, m_receivedSequences);
        }

        /// TODO Заменить на свой сторадж временных блоков
        if (--neighbours) {
            getPoolRequestedSequences(m_receivedSequences);
        }
    }

    if (!isRequested) {
        cs::Timer::singleShot(cs::NeighboursRequestDelay, [this] {
            sendBlockRequest(true);
        });
    }
}

void cs::PoolSynchronizer::reSendBlockRequestToRandomNeighbour() {
    cslog() << "POOL SYNCHRONIZER> Resend Block Request To Random Neighbour";

    if (m_timer.isRunning()) {
        m_timer.stop();
    }

    uint32_t lastSequence = m_blockChain->getLastWrittenSequence();

    m_receivedSequences.clear();
    csdebug() << "SOKOLOV> >>>>>>>>> sequence " << lastSequence;

    for (std::size_t i = 0; i < m_maxBlockCount; ++i) {
        ++lastSequence;
        // max sequence
        if (lastSequence > m_roundToSync) {
            break;
        }

        m_receivedSequences.push_back(lastSequence);
        csdebug() << "SOKOLOV> >>>>>>>>> sequence put on pools block " << lastSequence;
    }

    if (m_receivedSequences.size() == 0) {
        csdebug() << "POOL SYNCHRONIZER> Resend Block Request To Random Neighbour >>> !!!!!!!!!!!!!!!!!! sequences size is 0";
        return;
    }

    ConnectionPtr target = m_transport->getRandomNeighbour();

    if (!target) {
        csdebug() << "POOL SYNCHRONIZER> RBRTN No more free requestees";
    }

    sendBlock(target, m_receivedSequences);
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

void cs::PoolSynchronizer::sendBlock(const ConnectionPtr& target, const PoolsRequestedSequences& sequences) {
  csdebug() << "POOL SYNCHRONIZER> Sending block request : from nbr: " << target->getOut() << ", id: " << target->id;

  for (const auto& sequence : sequences) {
      cslog() << "POOL SYNCHRONIZER> Sending block request, sequence: " << sequence;

      if (!m_requestedSequences.count(sequence)) {
          m_requestedSequences.emplace(std::make_pair(sequence, m_maxWaitingTimeReply));
      }
  }

  emit sendRequest(target, sequences);
}

void cs::PoolSynchronizer::addToTemporaryStorage(const csdb::Pool& pool) {
    const auto sequence = pool.sequence();
    const auto transactionsCount = pool.transactions_count();

    if (!m_temporaryStorage.count(sequence)) {
        m_temporaryStorage.emplace(std::make_pair(sequence, pool));
        csdebug() << "POOL SYNCHRONIZER> Store received block: " << sequence << ",      with transactions: " << transactionsCount;
    }
}

void cs::PoolSynchronizer::processingTemporaryStorage() {
    if (!m_temporaryStorage.size()) {
        csdebug() << "POOL SYNCHRONIZER> temaporary storage is empty";
        return;
    }

    bool loop = true;
    csdb::Pool::sequence_t newSequence = 0;

    csdebug() << "POOL SYNCHRONIZER> temaporary storage contains: begin seq: " << m_temporaryStorage.begin()->first << ", end seq: " << m_temporaryStorage.rbegin()->first;

    while (loop) {
        newSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence() + 1);
        csdebug() << "POOL SYNCHRONIZER> Processing TemporaryStorage: needed sequence: " << newSequence;

        auto it = m_temporaryStorage.find(newSequence);

        if (it != m_temporaryStorage.end()) {
            csdebug() << "POOL SYNCHRONIZER> Temporary storage contains sequence: " << newSequence << ", with transactions: " << it->second.transactions_count();
            m_blockChain->onBlockReceived(it->second);
            m_temporaryStorage.erase(it);
        } else {
            csdebug() << "POOL SYNCHRONIZER> Processing TemporaryStorage: needed sequence: not contained in storage ";
            loop = false;
        }
    }
}

void cs::PoolSynchronizer::getPoolRequestedSequences(PoolsRequestedSequences& sequences) {
    uint32_t lastSequence = 0;
    bool isFromStorage = false;
    auto firstSequenceIt = m_requestedSequences.begin();

    sequences.clear();

    // if storage requested sequences is impty
    if (m_requestedSequences.empty()) {
        csdebug() << "SOKOLOV> >>>>>>>>> Get pool sequences   from   blockChain";
        lastSequence = m_blockChain->getLastWrittenSequence();
        csdebug() << "SOKOLOV> >>>>>>>>> sequence " << lastSequence;
    }
    else if (firstSequenceIt->second > 0) {
        csdebug() << "SOKOLOV> >>>>>>>>> Get pool sequences   from   end elemetns";
        const uint32_t lastSeqFromReqSeq = cs::numeric_cast<uint32_t>(m_requestedSequences.rbegin()->first);
        const uint32_t lastSeqFromTempStorage = m_temporaryStorage.empty() ? 0 : cs::numeric_cast<uint32_t>(m_temporaryStorage.rbegin()->first);

        lastSequence = std::max(lastSeqFromReqSeq, lastSeqFromTempStorage);
        csdebug() << "SOKOLOV> >>>>>>>>> sequence " << lastSequence;
    }
    else { // if maxWaitingTimeReply <= 0 for first element
        cslog() << "SOKOLOV> **************************************************************************************************************************************";
        csdebug() << "SOKOLOV> >>>>>>>>> Get pool sequences   from   first elemetns";
        lastSequence = cs::numeric_cast<uint32_t>(firstSequenceIt->first);
        isFromStorage = true;
        csdebug() << "SOKOLOV> >>>>>>>>> sequence " << lastSequence;
    }

    for (std::size_t i = 0; i < m_maxBlockCount; ++i) {
        if (!isFromStorage) {
            ++lastSequence;
        }

        // max sequence
        if (lastSequence > m_roundToSync) {
            break;
        }

        sequences.push_back(lastSequence);
        csdebug() << "SOKOLOV> >>>>>>>>> sequence put on pools block " << lastSequence;

        if (isFromStorage) {
            firstSequenceIt->second = m_maxWaitingTimeReply; // reset maxWaitingTimeReply
            ++firstSequenceIt; // next sequence
            lastSequence = cs::numeric_cast<uint32_t>(firstSequenceIt->first);
        }
    }
}
