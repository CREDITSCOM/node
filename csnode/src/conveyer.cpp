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
    cs::TransactionsPacketTable hashTable;

    // main conveyer meta data
    cs::ConveyerMetaStorage metaStorage;

    // characteristic meta base
    cs::CharacteristicMetaStorage characteristicMetas;

    // cached active current round number
    std::atomic<cs::RoundNumber> currentRound = 0;

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
    cs::TransactionsPacketHash hash = packet.hash();
    cs::Lock lock(m_sharedMutex);

    if (pimpl->hashTable.count(hash) == 0u) {
        pimpl->hashTable.emplace(std::move(hash), packet);
    }
    else {
        cswarning() << "CONVEYER> Can not add network transactions packet";
    }
}

const cs::TransactionsPacketTable& cs::Conveyer::transactionsPacketTable() const
{
    return pimpl->hashTable;
}

const cs::TransactionsBlock& cs::Conveyer::transactionsBlock() const
{
    return pimpl->transactionsBlock;
}

std::optional<cs::TransactionsPacket> cs::Conveyer::createPacket() const
{
    cs::ConveyerMeta* meta = pimpl->metaStorage.get(currentRoundNumber());

    if (!meta)
    {
        cserror() << "CONVEYER> Can not create transactions packet";
        return std::nullopt;
    }

    cs::TransactionsPacket packet;
    cs::Hashes& hashes = meta->roundTable.hashes;
    cs::TransactionsPacketTable& table = pimpl->hashTable;

    for (const auto& hash : hashes)
    {
        const auto iterator = table.find(hash);

        if (iterator == table.end())
        {
            cserror() << "CONVEYER>: PACKET CREATION HASH NOT FOUND";
            return std::nullopt;
        }

        const auto& transactions = iterator->second.transactions();

        for (const auto& transaction : transactions)
        {
            if (!packet.addTransaction(transaction)) {
                cserror() << "Can not add transaction to packet in consensus";
            }
        }
    }

    return std::make_optional<cs::TransactionsPacket>(std::move(packet));
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

    {
        cs::Lock lock(m_sharedMutex);
        pimpl->currentRound = table.round;
    }

    cs::ConveyerMetaStorage::Element element;
    element.round = table.round;
    element.meta.neededHashes = std::move(neededHashes);
    element.meta.roundTable = std::move(table);

    {
        cs::Lock lock(m_sharedMutex);

        if (!pimpl->metaStorage.contains(pimpl->currentRound)) {
            pimpl->metaStorage.append(element);
        }
        else {
            csfatal() << "CONVEYER> Meta round currently in conveyer";
        }
    }
}

const cs::RoundTable& cs::Conveyer::roundTable() const
{
    cs::ConveyerMeta* meta = pimpl->metaStorage.get(pimpl->currentRound);
    return meta->roundTable;
}

const cs::RoundTable* cs::Conveyer::roundTable(cs::RoundNumber round) const
{
    cs::ConveyerMeta* meta = pimpl->metaStorage.get(round);

    if (!meta) {
        return nullptr;
    }

    return &meta->roundTable;
}

cs::RoundNumber cs::Conveyer::currentRoundNumber() const
{
    return pimpl->currentRound;
}

const cs::Hashes& cs::Conveyer::currentNeededHashes() const
{
    return neededHashes(currentRoundNumber());
}

const cs::Hashes& cs::Conveyer::neededHashes(cs::RoundNumber round) const
{
    cs::ConveyerMeta* pointer = pimpl->metaStorage.get(round);

    if (!pointer) {
        throw std::out_of_range("Bad needed hashes, fatal error");
    }

    return pointer->neededHashes;
}

void cs::Conveyer::addFoundPacket(cs::RoundNumber round, cs::TransactionsPacket&& packet)
{
    cs::Lock lock(m_sharedMutex);

    cs::ConveyerMeta* metaPointer = pimpl->metaStorage.get(round);
    cs::TransactionsPacketTable* tablePointer = nullptr;

    if (metaPointer == nullptr)
    {
        cserror() << "CONVEYER> Can not add sync packet because meta pointer do not exist";
        return;
    }

    tablePointer = (round == pimpl->currentRound) ? &pimpl->hashTable : &metaPointer->hashTable;

    if (tablePointer == nullptr)
    {
        cserror() << "CONVEYER> Can not add sync packet because table pointer do not exist";
        return;
    }

    cs::Hashes& hashes = metaPointer->neededHashes;

    if (auto iterator = std::find(hashes.begin(), hashes.end(), packet.hash()); iterator != hashes.end())
    {
        csdebug() << "CONVEYER> Adding synced packet";
        hashes.erase(iterator);

        // add to current table
        auto hash = packet.hash();
        tablePointer->emplace(std::move(hash), std::move(packet));
    }
}

bool cs::Conveyer::isSyncCompleted() const
{
    return isSyncCompleted(currentRoundNumber());
}

bool cs::Conveyer::isSyncCompleted(cs::RoundNumber round) const
{
    cs::ConveyerMeta* meta = pimpl->metaStorage.get(round);

    if (!meta) {
        cserror() << "CONVEYER> Needed hashes of" << round << " round not found";
        return false;
    }

    return meta->neededHashes.empty();
}

const cs::Notifications& cs::Conveyer::notifications() const
{
    cs::ConveyerMeta* meta = pimpl->metaStorage.get(currentRoundNumber());

    if (meta) {
        return meta->notifications;
    }
    else {
        throw std::out_of_range("There is no notifications at current round");
    }
}

void cs::Conveyer::addNotification(const cs::Bytes& bytes)
{
    cs::ConveyerMeta* meta = pimpl->metaStorage.get(currentRoundNumber());

    if (meta)
    {
        csdebug() << "CONVEYER> Writer notification added";
        meta->notifications.push_back(bytes);
    }
}

std::size_t cs::Conveyer::neededNotificationsCount() const
{
    cs::ConveyerMeta* meta = pimpl->metaStorage.get(pimpl->currentRound);

    // TODO: check if +1 is correct
    if (meta) {
        return (meta->roundTable.confidants.size() / 2) + 1;
    }

    csdebug() << "CONVEYER> No notifications at current round";
    return 0;
}

bool cs::Conveyer::isEnoughNotifications(cs::Conveyer::NotificationState state) const
{
    cs::SharedLock lock(m_sharedMutex);

    const std::size_t neededConfidantsCount = neededNotificationsCount();
    const std::size_t notificationsCount = notifications().size();

    cslog() << "CONVEYER> Current notifications count - " << notificationsCount;
    cslog() << "CONVEYER> Needed confidans count - " << neededConfidantsCount;

    if (state == NotificationState::Equal) {
        return notificationsCount == neededConfidantsCount;
    }

    return notificationsCount >= neededConfidantsCount;
}

void cs::Conveyer::addCharacteristicMeta(RoundNumber round, CharacteristicMeta&& characteristic)
{
    if (!pimpl->characteristicMetas.contains(round))
    {
        cs::CharacteristicMetaStorage::Element metaElement;
        metaElement.meta = std::move(characteristic);
        metaElement.round = round;

        pimpl->characteristicMetas.append(std::move(metaElement));
    }
    else {
        csdebug() << "CONVEYER> Received meta is currently in meta stack";
    }
}

std::optional<cs::CharacteristicMeta> cs::Conveyer::characteristicMeta(const cs::RoundNumber round)
{
    if (!pimpl->characteristicMetas.contains(round))
    {
        csdebug() << "CONVEYER> Characteristic meta not received";
        return std::nullopt;
    }

    auto meta = pimpl->characteristicMetas.extract(round);
    return std::make_optional<cs::CharacteristicMeta>(std::move(meta).value());
}

void cs::Conveyer::setCharacteristic(const Characteristic& characteristic)
{
    cs::ConveyerMeta* meta = pimpl->metaStorage.get(pimpl->currentRound);

    if (meta)
    {
        csdebug() << "CONVEYER> Characteristic set to conveyer";
        meta->characteristic = characteristic;
    }
}

const cs::Characteristic& cs::Conveyer::characteristic() const
{
    auto meta = pimpl->metaStorage.get(pimpl->currentRound);
    return meta->characteristic;
}

cs::Hash cs::Conveyer::characteristicHash() const
{
    const Characteristic& reference = characteristic();
    return getBlake2Hash(reference.mask.data(), reference.mask.size());
}

std::optional<csdb::Pool> cs::Conveyer::applyCharacteristic(const cs::PoolMetaInfo& metaPoolInfo, const cs::PublicKey& sender)
{
    cslog() << "CONVEYER> ApplyCharacteristic";

    cs::Lock lock(m_sharedMutex);
    cs::ConveyerMeta* meta = pimpl->metaStorage.get(static_cast<cs::RoundNumber>(metaPoolInfo.sequenceNumber));

    if (!meta)
    {
        cserror() << "CONVEYER> Apply characteristic failed, no meta in meta storage";
        return std::nullopt;
    }

    const cs::Hashes& localHashes = meta->roundTable.hashes;
    cs::TransactionsPacketTable hashTable;

    const cs::Characteristic& characteristic = meta->characteristic;
    cs::TransactionsPacketTable& currentHashTable = pimpl->hashTable;

    cslog() << "CONVEYER> Characteristic bytes size " << characteristic.mask.size();

    csdb::Pool newPool;
    std::size_t maskIndex = 0;
    const cs::Bytes& mask = characteristic.mask;
    cs::TransactionsPacket invalidTransactions;

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
                else {
                    invalidTransactions.addTransaction(transaction);
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

    // add current round hashes to storage
    meta->hashTable = std::move(hashTable);
    meta->invalidTransactions = std::move(invalidTransactions);

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

    if (pimpl->hashTable.count(hash) != 0u) {
        return pimpl->hashTable[hash];
    }

    cs::ConveyerMeta* meta = pimpl->metaStorage.get(round);

    if (!meta) {
        return std::nullopt;
    }

    const auto& value = meta->hashTable;

    if (auto iter = value.find(hash); iter != value.end()) {
        return iter->second;
    }

    return std::nullopt;
}

bool cs::Conveyer::isMetaTransactionInvalid(int64_t id)
{
    cs::SharedLock lock(m_sharedMutex);

    for (const cs::ConveyerMetaStorage::Element& element : pimpl->metaStorage)
    {
        const auto& invalidTransactions = element.meta.invalidTransactions.transactions();
        const auto iterator = std::find_if(invalidTransactions.begin(), invalidTransactions.end(),
            [=](const auto& transaction) {
                return transaction.innerID() == id;
            });

        if (iterator != invalidTransactions.end()) {
            return true;
        }
    }

    return false;
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
                pimpl->hashTable.emplace(std::move(hash), std::move(packet));
            }
            else {
                cserror() << "CONVEYER > Logical error, adding transactions packet more than one time";
            }

            allTransactionsCount += transactionsCount;
        }
    }

    if (!pimpl->transactionsBlock.empty()) {
        pimpl->transactionsBlock.clear();
    }
}
