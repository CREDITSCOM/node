#include "csnode/conveyer.hpp"

#include <csdb/transaction.h>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <exception>

/// pointer implementation realization
struct cs::Conveyer::Impl
{
    // first storage of transactions, before sending to network
    cs::TransactionsBlock transactionsBlock;

    // current round transactions packets storage
    cs::TransactionsPacketHashTable hashTable;

    // sync fields
    cs::NeededHashesMetaStorage neededHashesMeta;

    // writer notifications
    cs::NotificationsMetaStorage notificationsMeta;

    // storage of received characteristic for slow motion nodes
    cs::CharacteristicMetaStorage characteristicMetas;
    cs::Characteristic characteristic;

    // hash tables storage
    cs::HashTablesMetaStorage hashTablesStorage;

    // round table
    cs::RoundTable roundTable;

public signals:
    cs::PacketFlushSignal flushPacket;
};


cs::Conveyer::Conveyer()
{
    pimpl = std::make_unique<cs::Conveyer::Impl>();
}

cs::Conveyer& cs::Conveyer::instance()
{
    static cs::Conveyer conveyer;
    return conveyer;
}

cs::PacketFlushSignal& cs::Conveyer::flushSignal()
{
    return pimpl->flushPacket;
}

void cs::Conveyer::addTransaction(const csdb::Transaction& transaction)
{
    if (!transaction.is_valid()) {
        cswarning() << "CONVEYER> Can not add no valid transaction to conveyer";
        return;
    }

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
    if (table.round <= currentRoundNumber())
    {
        cserror() << "CONVEYER> Setting round in conveyer failed";
        return;
    }

    const cs::Hashes& hashes = table.hashes;
    cs::Hashes neededHashes;

    {
        cs::SharedLock lock(m_sharedMutex);
        std::copy_if(hashes.begin(), hashes.end(), std::back_inserter(neededHashes), [this] (const auto& hash) {
            return (pimpl->hashTable.count(hash) == 0u);
        });
    }

    for (const auto& hash : neededHashes) {
        csdebug() << "CONVEYER> Need hash > " << hash.toString();
    }

    // stoge needed hashes
    if (!pimpl->neededHashesMeta.append(table.round, std::move(neededHashes))) {
        csfatal() << "CONVEYER> Undefined behaviour of conveyer or node, can not add round needed hashes";
    }

    {
        cs::Lock lock(m_sharedMutex);
        pimpl->roundTable = std::move(table);
    }

    // clean data
    if (!pimpl->notificationsMeta.contains(currentRoundNumber())) {
        pimpl->notificationsMeta.append(currentRoundNumber(), cs::Notifications());
    }
}

const cs::RoundTable& cs::Conveyer::roundTable() const
{
    return pimpl->roundTable;
}

cs::RoundNumber cs::Conveyer::currentRoundNumber() const
{
    cs::SharedLock lock(m_sharedMutex);
    return pimpl->roundTable.round;
}

const cs::Hashes& cs::Conveyer::currentNeededHashes() const
{
    auto pointer = pimpl->neededHashesMeta.get(currentRoundNumber());

    if (!pointer) {
        throw std::out_of_range("Bad needed hashes, fatal error");
    }

    return *pointer;
}

void cs::Conveyer::addFoundPacket(cs::RoundNumber round, cs::TransactionsPacket&& packet)
{
    cs::Lock lock(m_sharedMutex);

    cs::TransactionsPacketHashTable* tablePointer = nullptr;
    cs::Hashes* hashesPointer = nullptr;

    if (round == pimpl->roundTable.round) {
        tablePointer = &pimpl->hashTable;
    }
    else {
        tablePointer = pimpl->hashTablesStorage.get(round);
    }

    hashesPointer = pimpl->neededHashesMeta.get(round);

    if (tablePointer == nullptr || hashesPointer == nullptr) {
        cserror() << "CONVEYER> Can not add sync packet because hash table or needed hashes do not exist";
        return;
    }

    if (auto iterator = std::find(hashesPointer->begin(), hashesPointer->end(), packet.hash()); iterator != hashesPointer->end())
    {
        csdebug() << "CONVEYER> Adding synced packet";
        hashesPointer->erase(iterator);

        // add to current table
        tablePointer->emplace(packet.hash(), std::move(packet));
    }
}

bool cs::Conveyer::isSyncCompleted() const
{
    auto pointer = pimpl->neededHashesMeta.get(currentRoundNumber());

    if (!pointer) {
        cserror() << "CONVEYER> Needed hashes of current round not found";
        return false;
    }

    return pointer->empty();
}

bool cs::Conveyer::isSyncCompleted(cs::RoundNumber round) const
{
    auto pointer = pimpl->neededHashesMeta.get(round);

    if (!pointer) {
        cserror() << "CONVEYER> Needed hashes of" << round << " round not found";
        return false;
    }

    return pointer->empty();
}

const cs::Notifications& cs::Conveyer::notifications() const
{
    auto notifications = pimpl->notificationsMeta.get(currentRoundNumber());

    if (notifications) {
        return *notifications;
    }
    else {
        throw std::out_of_range("There is no notifications at current round");
    }
}

void cs::Conveyer::addNotification(const cs::Bytes& bytes)
{
    auto notifications = pimpl->notificationsMeta.get(currentRoundNumber());

    if (notifications)
    {
        csdebug() << "CONVEYER> Writer notification added";
        notifications->push_back(bytes);
    }
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
    const std::size_t notificationsCount = notifications().size();

    cslog() << "CONVEYER> Current notifications count - " << notificationsCount;
    cslog() << "CONVEYER> Needed confidans count - " << neededConfidantsCount;

    if (state == NotificationState::Equal) {
        return notificationsCount == neededConfidantsCount;
    }

    return notificationsCount >= neededConfidantsCount;
}

void cs::Conveyer::addCharacteristicMeta(cs::CharacteristicMetaStorage::MetaElement&& meta)
{
    if (!pimpl->characteristicMetas.append(meta)) {
        csdebug() << "CONVEYER> Received meta is currently in meta stack";
    }
}

std::optional<cs::CharacteristicMeta> cs::Conveyer::characteristicMeta(const cs::RoundNumber round)
{
    auto result = pimpl->characteristicMetas.extract(round);

    if (!result.has_value()) {
        cslog() << "CONVEYER> Characteristic meta not received";
        return std::nullopt;
    }

    return std::make_optional<cs::CharacteristicMeta>((std::move(result)).value());
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
            if (maskIndex < mask.size())
            {
                if (mask[maskIndex] != 0u) {
                    newPool.add_transaction(transaction);
                }
            }

            ++maskIndex;
        }

        if (maskIndex > mask.size())
        {
            cserror() << "CONVEYER: Apply characteristic hash failed, mask size: " << mask.size() << " mask index: " << maskIndex;
            return std::nullopt;
        }

        // create storage hash table and remove from current hash table
        hashTable.emplace(hash, std::move(packet));
        pimpl->hashTable.erase(hash);
    }

    cs::HashTablesMetaStorage::MetaElement element;
    element.round = pimpl->roundTable.round,
    element.meta = std::move(hashTable);

    // add current round hashes to storage
    pimpl->hashTablesStorage.append(element);

    if (characteristic.mask.size() != newPool.transactions_count())
    {
        cslog() << "CONVEYER> Characteristic size: " << characteristic.mask.size() << ", new pool transactions count: " << newPool.transactions_count();
        cswarning() << "CONVEYER> ApplyCharacteristic: Some of transactions is not valid";
    }

    cslog() << "CONVEYER> ApplyCharacteristic : sequence = " << metaPoolInfo.sequenceNumber;

    newPool.set_sequence(metaPoolInfo.sequenceNumber);
    newPool.add_user_field(0, metaPoolInfo.timestamp);

    const auto& writerPublicKey = sender;
    newPool.set_writer_public_key(csdb::internal::byte_array(writerPublicKey.begin(), writerPublicKey.end()));

    return std::make_optional<csdb::Pool>(std::move(newPool));
}

std::optional<cs::TransactionsPacket> cs::Conveyer::searchPacket(const cs::TransactionsPacketHash& hash, const RoundNumber round) const
{
    cs::SharedLock lock(m_sharedMutex);

    if (pimpl->hashTable.count(hash) != 0u)
    {
        csdebug() << "CONVEYER> Found hash at current table in request - " << hash.toString();
        return pimpl->hashTable[hash];
    }

    const auto optional = pimpl->hashTablesStorage.value(round);

    if (optional.has_value())
    {
        const auto& value = optional.value();
        csdebug() << "CONVEYER> Found round hash table in storage, searching hash";

        if (auto iter = value.find(hash); iter != value.end())
        {
            csdebug() << "CONVEYER> Found hash in hash table storage";
            return iter->second;
        }
    }

    csdebug() << "CONVEYER> Can not find round in storage, hash not found";
    return std::nullopt;
}

cs::SharedMutex& cs::Conveyer::sharedMutex() const
{
    return m_sharedMutex;
}

void cs::Conveyer::flushTransactions()
{
    cs::Lock lock(m_sharedMutex);
    std::size_t allTransactionsCount = 0;

    for (auto& packet : pimpl->transactionsBlock)
    {
        const std::size_t transactionsCount = packet.transactionsCount();

        if ((transactionsCount != 0u) && packet.isHashEmpty())
        {
            packet.makeHash();

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
