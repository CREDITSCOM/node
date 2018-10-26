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

    // sync fields
    cs::Hashes neededHashes;

    // writer notifications
    cs::Notifications notifications;

    // storage of received characteristic for slow motion nodes
    std::vector<cs::CharacteristicMeta> characteristicMetas;

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

void cs::Conveyer::addTransactionsPacket(const cs::TransactionsPacket& packet)
{
    csdebug() << "CONVEYER> Add transactions packet";

    cs::TransactionsPacketHash hash = packet.hash();
    cs::Lock lock(m_sharedMutex);

    if (!pimpl->hashTable.count(hash)) {
        pimpl->hashTable.emplace(hash, packet);
    }
    else {
        cswarning() << "CONVEYER> Can not add network transactions packet";
    }
}

const cs::TransactionsPacketHashTable& cs::Conveyer::transactionsPacketTable() const
{
    cs::SharedLock lock(m_sharedMutex);
    return pimpl->hashTable;
}

const cs::TransactionsBlock& cs::Conveyer::transactionsBlock() const
{
    cs::SharedLock lock(m_sharedMutex);
    return pimpl->transactionsBlock;
}

void cs::Conveyer::newRound(const cs::RoundTable& table)
{
    const cs::Hashes& hashes = table.hashes;
    cs::Hashes neededHashes;

    {
        cs::SharedLock lock(m_sharedMutex);

        for (const auto& hash : hashes)
        {
            if (!pimpl->hashTable.count(hash)) {
                neededHashes.push_back(hash);
            }
        }
    }

    for (const auto& hash : neededHashes) {
        csdebug() << "CONVEYER> Need hash > " << hash.toString();
    }

    pimpl->neededHashes = std::move(neededHashes);
}

const cs::Hashes& cs::Conveyer::neededHashes() const
{
    return pimpl->neededHashes;
}

bool cs::Conveyer::isSyncCompleted() const
{
    return pimpl->neededHashes.empty();
}

const cs::Notifications& cs::Conveyer::notifications() const
{
    return pimpl->notifications;
}

void cs::Conveyer::addNotification(const cs::Bytes& bytes)
{
    csdebug() << "CONVEYER> Writer notification added";
    pimpl->notifications.push_back(bytes);
}

std::size_t cs::Conveyer::neededNotificationsCount() const
{
    // TODO: check if +1 is correct
    const auto& roundTable = pimpl->solver->roundTable();
    return (roundTable.confidants.size() / 2) + 1;
}

bool cs::Conveyer::isEnoughNotifications(cs::Conveyer::NotificationState state) const
{
    const std::size_t neededConfidantsCount = neededNotificationsCount();
    const std::size_t notificationsCount = pimpl->notifications.size();

    cslog() << "CONVEYER> Current notifications count - " << notificationsCount;
    cslog() << "CONVEYER> Needed confidans count - " << neededConfidantsCount;

    if (state == NotificationState::Equal) {
        return notificationsCount == neededConfidantsCount;
    }
    else {
        return notificationsCount >= neededConfidantsCount;
    }
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
