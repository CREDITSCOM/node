#include "csnode/conveyer.h"

#include <csnode/node.hpp>
#include <solver/solver.hpp>
#include <csdb/transaction.h>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

/// pointer implementation realization
struct cs::Conveyer::Impl
{
    // other modules pointers
    Node* node{};

    // first storage of transactions, before sending to network
    cs::TransactionsBlock transactionsBlock;

    // current round transactions packets storage
    cs::TransactionsPacketHashTable hashTable;

    // sync fields
    cs::Hashes neededHashes;

    // writer notifications
    cs::Notifications notifications;

    // storage of received characteristic for slow motion nodes
    boost::circular_buffer<cs::CharacteristicMeta> characteristicMetas;
    cs::Characteristic characteristic;

    // hash tables storage
    cs::HashTablesStorage hashTablesStorage;

    // round table
    cs::RoundTable roundTable;

signals:
    cs::PacketFlushSignal flushPacket;
};


cs::Conveyer::Conveyer()
{
    pimpl = std::make_unique<cs::Conveyer::Impl>();
    pimpl->hashTablesStorage.resize(HashTablesStorageCapacity);
    pimpl->characteristicMetas.resize(CharacteristicMetaCapacity);

    m_sendingTimer.connect(std::bind(&cs::Conveyer::flushTransactions, this));
}

cs::Conveyer::~Conveyer()
{
    m_sendingTimer.disconnect();
    m_sendingTimer.stop();
}

cs::Conveyer& cs::Conveyer::instance()
{
    static cs::Conveyer conveyer;
    return conveyer;
}

void cs::Conveyer::setNode(Node* node)
{
    pimpl->node = node;
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

    if (pimpl->hashTable.count(hash) == 0u) {
        pimpl->hashTable.emplace(hash, packet);
    }
    else {
        cswarning() << "CONVEYER> Can not add network transactions packet";
    }
}

const cs::TransactionsPacketHashTable& cs::Conveyer::transactionsPacketTable() const
{
    return pimpl->hashTable;
}

const cs::TransactionsBlock& cs::Conveyer::transactionsBlock() const
{
    return pimpl->transactionsBlock;
}

const cs::TransactionsPacket& cs::Conveyer::packet(const cs::TransactionsPacketHash& hash) const
{
    return pimpl->hashTable[hash];
}

void cs::Conveyer::setRound(cs::RoundTable&& table)
{
    if (table.round <= pimpl->roundTable.round)
    {
        cserror() << "CONVEYER> Setting round in conveyer failed";
        return;
    }

    const cs::Hashes& hashes = table.hashes;
    cs::Hashes neededHashes;

    {
        cs::SharedLock lock(m_sharedMutex);

        for (const auto& hash : hashes)
        {
            if (pimpl->hashTable.count(hash) == 0u) {
                neededHashes.push_back(hash);
            }
        }
    }

    for (const auto& hash : neededHashes) {
        csdebug() << "CONVEYER> Need hash > " << hash.toString();
    }

    // stoge needed hashes
    pimpl->neededHashes = std::move(neededHashes);

    {
        cs::Lock lock(m_sharedMutex);
        pimpl->roundTable = std::move(table);
    }

    if (!m_sendingTimer.isRunning())
    {
        cslog() << "CONVEYER> Transaction timer started";
        m_sendingTimer.start(TransactionsPacketInterval);
    }

    // clean data
    pimpl->notifications.clear();
}

const cs::RoundTable& cs::Conveyer::roundTable() const
{
    return pimpl->roundTable;
}

cs::RoundNumber cs::Conveyer::roundNumber() const
{
    cs::SharedLock lock(m_sharedMutex);
    return pimpl->roundTable.round;
}

const cs::RoundTable cs::Conveyer::roundTableSafe() const
{
    cs::SharedLock lock(m_sharedMutex);
    return pimpl->roundTable;
}

const cs::Hashes& cs::Conveyer::neededHashes() const
{
    return pimpl->neededHashes;
}

void cs::Conveyer::addFoundPacket(cs::TransactionsPacket&& packet)
{
    cs::Lock lock(m_sharedMutex);
    auto& hashes = pimpl->neededHashes;

    if (auto iterator = std::find(hashes.begin(), hashes.end(), packet.hash()); iterator != hashes.end())
    {
        csdebug() << "CONVEYER> Adding synced packet";
        hashes.erase(iterator);

        // add to current table
        pimpl->hashTable.emplace(packet.hash(), std::move(packet));
    }
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
    const auto& roundTable = pimpl->roundTable;
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
    return notificationsCount >= neededConfidantsCount;
}

void cs::Conveyer::addCharacteristicMeta(const cs::CharacteristicMeta& meta)
{
    auto& metas = pimpl->characteristicMetas;
    const auto iterator = std::find(metas.begin(), metas.end(), meta);

    if (iterator != metas.end())
    {
        metas.push_back(meta);
        csdebug() << "CONVEYER> Characteristic meta added";
    }
    else {
        csdebug() << "CONVEYER> Received meta is currently in meta stack";
    }
}

std::optional<cs::CharacteristicMeta> cs::Conveyer::characteristicMeta(const cs::RoundNumber round)
{
    cs::CharacteristicMeta meta;
    meta.round = round;

    auto& metas = pimpl->characteristicMetas;
    const auto iterator = std::find(metas.begin(), metas.end(), meta);

    if (iterator == metas.end()) {
        cserror() << "CONVEYER> Characteristic meta not found";
        return std::nullopt;
    }

    meta = std::move(*iterator);
    metas.erase(iterator);

    return meta;
}

void cs::Conveyer::setCharacteristic(const cs::Characteristic& characteristic)
{
    csdebug() << "CONVEYER> Characteristic set to conveyer";
    pimpl->characteristic = characteristic;
}

const cs::Characteristic& cs::Conveyer::characteristic() const
{
    return pimpl->characteristic;
}

cs::Hash cs::Conveyer::characteristicHash() const
{
    const Characteristic& characteristic = pimpl->characteristic;
    return getBlake2Hash(characteristic.mask.data(), characteristic.mask.size());
}

std::optional<csdb::Pool> cs::Conveyer::applyCharacteristic(const cs::PoolMetaInfo& metaPoolInfo, const cs::PublicKey& sender)
{
    cslog() << "CONVEYER> ApplyCharacteristic";

    cs::Lock lock(m_sharedMutex);

    const cs::Hashes& localHashes = pimpl->roundTable.hashes;
    cs::TransactionsPacketHashTable hashTable;

    const cs::Characteristic& characteristic = pimpl->characteristic;
    cs::TransactionsPacketHashTable& currentHashTable = pimpl->hashTable;

    cslog() << "CONVEYER> Characteristic bytes size " << characteristic.mask.size();

    csdb::Pool newPool;
    std::size_t maskIndex = 0;
    const cs::Bytes& mask = characteristic.mask;

    for (const auto& hash : localHashes)
    {
        if (currentHashTable.count(hash) == 0u)
        {
            cserror() << "CONVEYER> ApplyCharacteristic: HASH NOT FOUND " << hash.toString();
            return std::nullopt;
        }

        auto& packet = currentHashTable[hash];
        const auto& transactions = packet.transactions();

        for (const auto& transaction : transactions)
        {
            if (mask.at(maskIndex) != 0u) {
                newPool.add_transaction(transaction);
            }

            ++maskIndex;
        }

        // create storage hash table and remove from current hash table
        hashTable.emplace(hash, std::move(packet));
        pimpl->hashTable.erase(hash);
    }

    cs::StorageElement element;
    element.round = pimpl->roundTable.round;
    element.hashTable = std::move(hashTable);

    // add current round hashes to storage
    pimpl->hashTablesStorage.push_back(element);

    if (characteristic.mask.size() != newPool.transactions_count()) {
        cslog() << "CONVEYER> Characteristic size: " << characteristic.mask.size() << ", new pool transactions count: " << newPool.transactions_count();
        cswarning() << "CONVEYER> ApplyCharacteristic: Some of transactions is not valid";
    }

    cslog() << "CONVEYER> ApplyCharacteristic : sequence = " << metaPoolInfo.sequenceNumber;

    newPool.set_sequence(metaPoolInfo.sequenceNumber);
    newPool.add_user_field(0, metaPoolInfo.timestamp);

    const auto& writerPublicKey = sender;
    newPool.set_writer_public_key(csdb::internal::byte_array(writerPublicKey.begin(), writerPublicKey.end()));

    return newPool;
}

std::optional<cs::TransactionsPacket> cs::Conveyer::searchPacket(const cs::TransactionsPacketHash& hash, const RoundNumber round)
{
    cs::SharedLock lock(m_sharedMutex);

    if (pimpl->hashTable.count(hash))
    {
        csdebug() << "CONVEYER> Found hash at current table in request - " << hash.toString();
        return pimpl->hashTable[hash];
    }

    const auto& storage = pimpl->hashTablesStorage;
    const auto iterator = std::find_if(storage.begin(), storage.end(), [&](const cs::StorageElement& element) {
        return element.round == round;
    });

    if (iterator != storage.end())
    {
        csdebug() << "CONVEYER> Found round hash table in storage, searching hash";

        if (auto iter = iterator->hashTable.find(hash); iter != iterator->hashTable.end())
        {
            csdebug() << "CONVEYER> Found hash in hash table storage";
            return iter->second;
        }
    }

    csdebug() << "CONVEYER> Can not find round in storage, hash not found";
    return std::nullopt;
}

cs::SharedMutex &cs::Conveyer::sharedMutex() const
{
    return m_sharedMutex;
}

void cs::Conveyer::flushTransactions()
{
    cs::Lock lock(m_sharedMutex);
    const auto round = pimpl->roundTable.round;

    if (pimpl->node->getNodeLevel() != NodeLevel::Normal || round <= TransactionsFlushRound) {
        return;
    }

    std::size_t allTransactionsCount = 0;

    for (auto& packet : pimpl->transactionsBlock)
    {
        const std::size_t transactionsCount = packet.transactionsCount();

        if ((transactionsCount != 0u) && packet.isHashEmpty())
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

            // try to send save in node
            pimpl->flushPacket(packet);

            auto hash = packet.hash();

            if (hash.isEmpty()) {
                cserror() << "CONVEYER > Transaction packet hashing failed";
            }

            if (pimpl->hashTable.count(hash) == 0u) {
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
