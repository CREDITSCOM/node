#include "csnode/conveyer.h"

#include <solver2/SolverCore.h>
#include <csdb/transaction.h>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

/// pointer implementation realization
struct cs::Conveyer::Impl
{
    Impl();
    ~Impl();

    // other modules pointers
    slv2::SolverCore* solver;

    // first storage of transactions, before sending to network
    cs::TransactionsBlock transactionsBlock;

    // current round transactions packets storage
    cs::TransactionsPacketHashTable hashTable;

signals:
    cs::PacketFlushSignal flushPacket;
};

cs::Conveyer::Impl::Impl():
    solver(nullptr)
{
}

cs::Conveyer::Impl::~Impl()
{
    solver = nullptr;
}

cs::Conveyer::Conveyer()
{
    pimpl = std::make_unique<cs::Conveyer::Impl>();
    m_sendingTimer.connect(std::bind(&cs::Conveyer::flushTransactions, this));
}

cs::Conveyer::~Conveyer()
{
    m_sendingTimer.disconnect();
    m_sendingTimer.stop();
}

cs::Conveyer* cs::Conveyer::instance()
{
    static cs::Conveyer conveyer;
    return &conveyer;
}

void cs::Conveyer::setSolver(slv2::SolverCore* solver)
{
    pimpl->solver = solver;
}

cs::PacketFlushSignal& cs::Conveyer::flushSignal()
{
    return pimpl->flushPacket;
}

void cs::Conveyer::addTransaction(const csdb::Transaction& transaction)
{
    cs::Lock lock(m_sharedMutex);

    if (pimpl->transactionsBlock.empty() || (pimpl->transactionsBlock.back().transactionsCount() >= MaxPacketTransactions)) {
        pimpl->transactionsBlock.push_back(cs::TransactionsPacket());
    }

    pimpl->transactionsBlock.back().addTransaction(transaction);
}

void cs::Conveyer::flushTransactions()
{
    cs::Lock lock(m_sharedMutex);
    const auto& roundTable = pimpl->solver->roundTable();

    if (pimpl->solver->nodeLevel() != NodeLevel::Normal || roundTable.round <= TransactionsFlushRound) {
        return;
    }

    std::size_t allTransactionsCount = 0;

    for (auto& packet : pimpl->transactionsBlock)
    {
        const std::size_t transactionsCount = packet.transactionsCount();

        if (transactionsCount && packet.isHashEmpty())
        {
            packet.makeHash();

            for (const auto& transaction : packet.transactions())
            {
                if (!transaction.is_valid())
                {
                    cswarning() << "CONVEYER > Can not send not valid transaction, sorry";
                    continue;
                }
            }

            pimpl->flushPacket(packet);

            auto hash = packet.hash();

            if (hash.isEmpty()) {
                cserror() << "CONVEYER > Transaction packet hashing failed";
            }

            if (!pimpl->hashTable.count(hash)) {
                pimpl->hashTable.emplace(hash, packet);
            }
            else {
                cserror() << "CONVEYER > Logical error, adding transactions packet more than one time";
            }

            allTransactionsCount += transactionsCount;
        }
    }

    if (!pimpl->transactionsBlock.empty())
    {
        csdebug() << "CONVEYER> All transaction packets flushed, packets count: " << pimpl->transactionsBlock.size();
        csdebug() << "CONVEYER> Common flushed transactions count: " << allTransactionsCount;

        pimpl->transactionsBlock.clear();
    }
}
