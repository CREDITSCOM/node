#include "csnode/conveyer.hpp"

#include <exception>
#include <iomanip>

#include <csdb/transaction.hpp>

#include <csnode/configholder.hpp>
#include <csnode/datastream.hpp>
#include <csnode/sendcachedata.hpp>

#include <solver/smartcontracts.hpp>

#include <lib/system/hash.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

namespace {
cs::ConveyerBase* conveyerView = nullptr;
std::once_flag onceFlag = {};

static void setup(cs::ConveyerBase* conveyer) {
    conveyerView = conveyer;
}
}

struct cs::ConveyerBase::Impl {
    explicit Impl(size_t queueSize, size_t transactionsSize, size_t packetsPerRound, size_t metaSize);

    // first storage of transactions, before sending to network
    cs::PacketQueue packetQueue;

    // current round transactions packets storage
    cs::TransactionsPacketTable packetsTable;

    // main conveyer meta data
    cs::ConveyerMetaStorage metaStorage;

    // characteristic meta base
    cs::CharacteristicMetaStorage characteristicMetas;

    // cached active current round number
    std::atomic<cs::RoundNumber> currentRound = 0;

    // to sign transaction packets
    cs::PrivateKey privateKey;

    // helpers
    const cs::ConveyerMeta* validMeta() &;
};

inline cs::ConveyerBase::Impl::Impl(size_t queueSize, size_t transactionsSize, size_t packetsPerRound, size_t metaSize)
: packetQueue(queueSize, transactionsSize, packetsPerRound)
, metaStorage(metaSize) {
}

inline const cs::ConveyerMeta* cs::ConveyerBase::Impl::validMeta() & {
    cs::ConveyerMeta* meta = metaStorage.get(currentRound);

    if (meta != nullptr) {
        return meta;
    }

    return &(metaStorage.max());
}

cs::ConveyerBase::ConveyerBase() {
    pimpl_ = std::make_unique<cs::ConveyerBase::Impl>(MaxQueueSize, MaxPacketTransactions, MaxPacketsPerRound, MetaCapacity);
    pimpl_->metaStorage.append(cs::ConveyerMetaStorage::Element());

    std::call_once(::onceFlag, &::setup, this);
    cs::Connector::connect(&roundChanged, this, &cs::Conveyer::onRoundChanged);
}

void cs::ConveyerBase::setPrivateKey(const cs::PrivateKey& privateKey) {
    pimpl_->privateKey = privateKey;
}

void cs::ConveyerBase::setRound(cs::RoundNumber round) {
    csmeta(csdebug) << "trying to change round to " << round;

    if (currentRoundNumber() < round) {
        pimpl_->currentRound = round;
        csdebug() << csname() << "cached round updated";

        emit roundChanged(round);
    }
    else {
        cswarning() << csname() << "current round " << currentRoundNumber();
    }
}

cs::ConveyerBase::~ConveyerBase() = default;

void cs::ConveyerBase::addTransaction(const csdb::Transaction& transaction) {
    if (!transaction.is_valid()) {
        cswarning() << csname() << "Can not add no valid transaction to conveyer";
        return;
    }

    cs::Lock lock(sharedMutex_);

    auto id = transaction.innerID();

    if (pimpl_->packetQueue.push(transaction)) {
        csdetails() << csname() << "Add valid transaction to conveyer id: " << id << ", queue size: " << pimpl_->packetQueue.size();
    }
    else {
        cswarning() << csname() << "Add transaction failed to queue, transaction id: " << id << ", queue size: " << pimpl_->packetQueue.size();
    }
}

void cs::ConveyerBase::addContractPacket(cs::TransactionsPacket& packet) {
    cs::TransactionsPacketHash hash = packet.hash();
    csdebug() << csname() << "Add separate transactions packet to conveyer, transactions " << packet.transactionsCount();
    cs::Lock lock(sharedMutex_);

    if (auto iterator = pimpl_->packetsTable.find(hash); iterator == pimpl_->packetsTable.end()) {
        // add current packet
        pimpl_->packetQueue.push(packet);
    }
    else {
        csdebug() << csname() << "Same separate packet already is in table: " << hash.toString();
    }
}

void cs::ConveyerBase::addTransactionsPacket(const cs::TransactionsPacket& packet) {
    auto round = currentRoundNumber();

    if (round > packet.expiredRound()) {
        csdebug() << csname() << "Ignore expired packet, expired round: " << packet.expiredRound() << ", current round " <<
                     round << "hash " << packet.hash().toString();
        return;
    }

    cs::TransactionsPacketHash hash = packet.hash();
    cs::Lock lock(sharedMutex_);

    if (!isPacketAtCache(packet)) {
        pimpl_->packetsTable.emplace(std::move(hash), packet);
    }
    else {
        csdebug() << csname() << "Same hash already exists at table: " << hash.toString();
    }
}

const cs::TransactionsPacketTable& cs::ConveyerBase::transactionsPacketTable() const {
    return pimpl_->packetsTable;
}

const cs::PacketQueue& cs::ConveyerBase::packetQueue() const {
    return pimpl_->packetQueue;
}

std::optional<std::pair<cs::TransactionsPacket, cs::Packets>> cs::ConveyerBase::createPacket(cs::RoundNumber round) const {
    cs::Lock lock(sharedMutex_);

    static constexpr size_t smartContractDetector = 1;
    cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

    if (!meta) {
        cserror() << csname() << "Can not create transactions packet at round " << round;
        return std::nullopt;
    }

    cs::TransactionsPacket packet;
    cs::Packets smartContractPackets;

    cs::PacketsHashes& hashes = meta->roundTable.hashes;
    cs::TransactionsPacketTable& table = pimpl_->packetsTable;

    for (const auto& hash : hashes) {
        const auto iterator = table.find(hash);

        if (iterator == table.end()) {
            cswarning() << csname() << "packet creation hash not found";
            return std::nullopt;
        }

        // to smarts
        if (iterator->second.signatures().size() > smartContractDetector) {
            smartContractPackets.push_back(iterator->second);
        }

        const auto& transactions = iterator->second.transactions();

        for (const auto& transaction : transactions) {
            if (!packet.addTransaction(transaction)) {
                cswarning() << csname() << "Can not add transaction at packet creation";
            }
        }
    }

    auto data = std::make_pair<cs::TransactionsPacket, cs::Packets>(std::move(packet), std::move(smartContractPackets));
    return std::make_optional<decltype(data)>(std::move(data));
}

void cs::ConveyerBase::updateRoundTable(cs::RoundNumber cachedRound, const cs::RoundTable& table) {
    cslog() << csname() << "updateRoundTable";

    {
        cs::Lock lock(sharedMutex_);

        while (table.round <= cachedRound) {
            pimpl_->metaStorage.extract(cachedRound);
            --cachedRound;
        }

        if (pimpl_->metaStorage.contains(table.round)) {
            cserror() << csname() << "Round table updation failed";
        }
    }

    setTable(table);
}

void cs::ConveyerBase::setTable(const RoundTable& table) {
    csmeta(csdebug) << "started";

    if (table.round < currentRoundNumber()) {
        cserror() << csname() << "Setting table in conveyer failed, current round " << currentRoundNumber() << ", table round " << table.round;
        return;
    }

    const cs::PacketsHashes& hashes = table.hashes;
    cs::PacketsHashes neededHashes;

    {
        cs::SharedLock lock(sharedMutex_);
        std::copy_if(hashes.begin(), hashes.end(), std::back_inserter(neededHashes), [this](const auto& hash) {
            return (pimpl_->packetsTable.count(hash) == 0u);
        });
    }

    csdebug() << csname() << "Needed round hashes count " << neededHashes.size();

    for (const auto& hash : neededHashes) {
        csdetails() << csname() << "Need hash " << hash.toString();
    }

    changeRound(table.round);

    cs::ConveyerMetaStorage::Element element;
    element.round = table.round;
    element.meta.neededHashes = std::move(neededHashes);
    element.meta.roundTable = table;

    {
        cs::Lock lock(sharedMutex_);

        if (!pimpl_->metaStorage.contains(pimpl_->currentRound)) {
            pimpl_->metaStorage.append(std::move(element));
        }
        else {
            csfatal() << csname() << "Meta round currently in conveyer";
        }
    }

    csmeta(csdebug) << "done, current table size " << pimpl_->packetsTable.size();
}

const cs::RoundTable& cs::ConveyerBase::currentRoundTable() const {
    return pimpl_->validMeta()->roundTable;
}

const cs::ConfidantsKeys& cs::ConveyerBase::confidants() const {
    return currentRoundTable().confidants;
}

size_t cs::ConveyerBase::confidantsCount() const {
    return confidants().size();
}

bool cs::ConveyerBase::isConfidantExists(size_t index) const {
    const cs::ConfidantsKeys& confidantsReference = confidants();

    if (confidantsReference.size() <= index) {
        csmeta(cserror) << ", index " << index << " out of range , confidants count " << confidantsReference.size() << ", on round " << pimpl_->currentRound;
        return false;
    }

    return true;
}

bool cs::ConveyerBase::isConfidantExists(const cs::PublicKey& confidant) const {
    const cs::ConfidantsKeys& keys = confidants();
    auto iterator = std::find(keys.begin(), keys.end(), confidant);
    return iterator != keys.end();
}

const cs::PublicKey& cs::ConveyerBase::confidantByIndex(size_t index) const {
    return confidants()[index];
}

std::optional<cs::PublicKey> cs::ConveyerBase::confidantIfExists(size_t index) const {
    if (isConfidantExists(index)) {
        return confidants()[index];
    }

    return std::nullopt;
}

const cs::RoundTable* cs::ConveyerBase::roundTable(cs::RoundNumber round) const {
    cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

    if (!meta) {
        return nullptr;
    }

    return &meta->roundTable;
}

cs::RoundNumber cs::ConveyerBase::currentRoundNumber() const {
    return pimpl_->currentRound;
}

cs::RoundNumber cs::ConveyerBase::previousRoundNumber() const {
    return pimpl_->currentRound - 1;
}

const cs::PacketsHashes& cs::ConveyerBase::currentNeededHashes() const {
    return pimpl_->validMeta()->neededHashes;
}

const cs::PacketsHashes* cs::ConveyerBase::neededHashes(cs::RoundNumber round) const {
    cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

    if (!meta) {
        cserror() << csname() << "Bad needed hashes, check node logic";
        return nullptr;
    }

    return &(meta->neededHashes);
}

void cs::ConveyerBase::addFoundPacket(cs::RoundNumber round, cs::TransactionsPacket&& packet) {
    cs::Lock lock(sharedMutex_);

    cs::ConveyerMeta* metaPointer = pimpl_->metaStorage.get(round);
    cs::TransactionsPacketTable* tablePointer = nullptr;

    if (metaPointer == nullptr) {
        cserror() << csname() << "Can not add sync packet because meta pointer do not exist";
        return;
    }

    tablePointer = (round == pimpl_->currentRound) ? &pimpl_->packetsTable : &metaPointer->hashTable;

    if (tablePointer == nullptr) {
        cserror() << csname() << "Can not add sync packet because table pointer do not exist";
        return;
    }

    cs::PacketsHashes& hashes = metaPointer->neededHashes;

    if (auto iterator = std::find(hashes.begin(), hashes.end(), packet.hash()); iterator != hashes.end()) {
        csdebug() << csname() << "Adding synced packet";
        hashes.erase(iterator);

        // add to current table
        auto hash = packet.hash();
        tablePointer->emplace(std::move(hash), std::move(packet));
    }
}

bool cs::ConveyerBase::isSyncCompleted() const {
    return isSyncCompleted(currentRoundNumber());
}

bool cs::ConveyerBase::isSyncCompleted(cs::RoundNumber round) const {
    cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

    if (!meta) {
        cswarning() << csname() << "Needed hashes of " << round << " round not found";
        return true;
    }

    return meta->neededHashes.empty();
}

void cs::ConveyerBase::addCharacteristicMeta(RoundNumber round, CharacteristicMeta&& characteristic) {
    if (!pimpl_->characteristicMetas.contains(round)) {
        cs::CharacteristicMetaStorage::Element metaElement;
        metaElement.meta = std::move(characteristic);
        metaElement.round = round;

        pimpl_->characteristicMetas.append(std::move(metaElement));
    }
    else {
        csdebug() << csname() << "Received meta is currently in meta stack";
    }
}

std::optional<cs::CharacteristicMeta> cs::ConveyerBase::characteristicMeta(const cs::RoundNumber round) {
    if (!pimpl_->characteristicMetas.contains(round)) {
        csdebug() << csname() << "Characteristic meta not received";
        return std::nullopt;
    }

    auto meta = pimpl_->characteristicMetas.extract(round);
    return std::make_optional<cs::CharacteristicMeta>(std::move(meta).value());
}

void cs::ConveyerBase::setCharacteristic(const Characteristic& characteristic, cs::RoundNumber round) {
    cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

    if (meta != nullptr) {
        csdebug() << csname() << "Characteristic set to conveyer, #" << round;
        meta->characteristic = characteristic;
    }
}

const cs::Characteristic* cs::ConveyerBase::characteristic(cs::RoundNumber round) const {
    auto meta = pimpl_->metaStorage.get(round);

    if (!meta) {
        cserror() << csname() << "Get characteristic, logic error, can not find characteristic, #" << round;
        return nullptr;
    }

    return &meta->characteristic;
}

cs::Hash cs::ConveyerBase::characteristicHash(cs::RoundNumber round) const {
    const Characteristic* pointer = characteristic(round);

    if (!pointer) {
        cserror() << csname() << "Null pointer of characteristic, return empty Hash, #" << round;
        return cs::Hash();
    }

    return generateHash(pointer->mask.data(), pointer->mask.size());
}

std::optional<csdb::Pool> cs::ConveyerBase::applyCharacteristic(const cs::PoolMetaInfo& metaPoolInfo) {
    cs::RoundNumber round = static_cast<cs::RoundNumber>(metaPoolInfo.sequenceNumber);
    csmeta(csdetails) << ", round " << round;

    cs::Lock lock(sharedMutex_);
    cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

    if (!meta) {
        cserror() << csname() << "Apply characteristic failed, no meta in meta storage";
        return std::nullopt;
    }

    cs::TransactionsPacketTable hashTable;
    const cs::PacketsHashes& localHashes = meta->roundTable.hashes;
    const cs::Characteristic& characteristic = meta->characteristic;
    cs::TransactionsPacketTable& currentHashTable = poolTable(round);

    csmeta(csdebug) << "characteristic bytes size " << characteristic.mask.size();

    if (!characteristic.mask.empty()) {
        csmeta(csdetails) << "characteristic: " << cs::Utils::byteStreamToHex(characteristic.mask.data(), characteristic.mask.size());
    }

    csmeta(csdebug) << "viewing hashes count " << localHashes.size();
    csmeta(csdebug) << "viewing hash table size " << currentHashTable.size();

    csdb::Pool newPool;
    std::size_t maskIndex = 0;
    const cs::Bytes& mask = characteristic.mask;
    cs::TransactionsPacket invalidTransactions;
    std::vector<csdb::Transaction> stateTransactions;

    bool isStateRejected = false;

    for (const auto& hash : localHashes) {
        // try to get from meta if can
        auto optionalPacket = findPacket(hash, round);

        if (!optionalPacket.has_value()) {
            csmeta(cserror) << "hash not found " << hash.toString() << ", strange behaviour detected";
            removeHashesFromTable(localHashes);
            return std::nullopt;
        }

        auto packet = std::move(optionalPacket).value();
        const auto& transactions = packet.transactions();

        // first look at signatures if it is smarts packet
        if (packet.isSmart()) {
            const auto& stateTransaction = transactions.front();

            // check range
            if (maskIndex < mask.size()) {
                if (mask[maskIndex] != 0) {
                    csdb::Pool::SmartSignature smartSignatures;
                    csdb::UserField userField = stateTransaction.user_field(trx_uf::new_state::RefStart);

                    if (userField.is_valid()) {
                        SmartContractRef reference(userField);

                        if (reference.is_valid()) {
                            smartSignatures.smartConsensusPool = reference.sequence;
                        }
                    }

                    smartSignatures.smartKey = stateTransaction.source().public_key();
                    smartSignatures.signatures = packet.signatures();

                    newPool.add_smart_signature(smartSignatures);
                }
                else {
                    isStateRejected = true;
                }
            }

            // add states to cache
            if (!isStateRejected) {
                for (const auto& transaction : packet.stateTransactions()) {
                    stateTransactions.push_back(transaction);
                }
            }
        }

        // look all next transactions
        for (const auto& transaction : transactions) {
            if (maskIndex < mask.size()) {
                if (mask[maskIndex] != 0u) {
                    newPool.add_transaction(transaction);
                }
                else {
                    invalidTransactions.addTransaction(transaction);
                }
            }

            ++maskIndex;
        }

        if (maskIndex > mask.size()) {
            csmeta(cserror) << "hash failed, mask size: " << mask.size() << " mask index: " << maskIndex;
            removeHashesFromTable(localHashes);
            return std::nullopt;
        }

        // create storage hash table and remove from current hash table
        hashTable.emplace(hash, std::move(packet));
    }

    // remove current hashes from table
    removeHashesFromTable(localHashes);

    csdebug() << "\tinvalid transactions count " << invalidTransactions.transactionsCount();

    // add current round hashes to storage
    meta->hashTable = std::move(hashTable);
    meta->invalidTransactions = std::move(invalidTransactions);

    if (characteristic.mask.size() != newPool.transactions_count()) {
        auto cnt_total = characteristic.mask.size();
        auto cnt_valid = newPool.transactions_count();
        cslog() << "Viewed transactions: " << cnt_total << ", valid : " << cnt_valid << ", invalid: " << cnt_total - cnt_valid;
    }

    csdebug() << "\tsequence = " << metaPoolInfo.sequenceNumber;

    // creating new pool
    newPool.set_sequence(metaPoolInfo.sequenceNumber);
    newPool.add_user_field(0, metaPoolInfo.timestamp);
    newPool.add_number_trusted(static_cast<uint8_t>(metaPoolInfo.realTrustedMask.size()));
    newPool.add_real_trusted(cs::Utils::maskToBits(metaPoolInfo.realTrustedMask));
    newPool.set_previous_hash(metaPoolInfo.previousHash);

    csmeta(csdetails) << "done";

    if (!stateTransactions.empty()) {
        emit statesCreated(stateTransactions);
    }

    return std::make_optional<csdb::Pool>(std::move(newPool));
}

std::optional<cs::TransactionsPacket> cs::ConveyerBase::findPacket(const cs::TransactionsPacketHash& hash, const RoundNumber round) const {
    if (auto iterator = pimpl_->packetsTable.find(hash); iterator != pimpl_->packetsTable.end()) {
        return iterator->second;
    }

    cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

    if (!meta) {
        return std::nullopt;
    }

    const auto& value = meta->hashTable;

    if (auto iter = value.find(hash); iter != value.end()) {
        return iter->second;
    }

    return std::nullopt;
}

bool cs::ConveyerBase::isMetaTransactionInvalid(int64_t id) {
    cs::SharedLock lock(sharedMutex_);

    for (const cs::ConveyerMetaStorage::Element& element : pimpl_->metaStorage) {
        const auto& invalidTransactions = element.meta.invalidTransactions.transactions();
        const auto iterator = std::find_if(invalidTransactions.begin(), invalidTransactions.end(), [=](const auto& transaction) {
            return transaction.innerID() == id;
        });

        if (iterator != invalidTransactions.end()) {
            return true;
        }
    }

    return false;
}

size_t cs::ConveyerBase::packetQueueTransactionsCount() const {
    cs::SharedLock lock(sharedMutex_);
    size_t count = 0;

    auto begin = pimpl_->packetQueue.begin();
    auto end = pimpl_->packetQueue.end();

    std::for_each(begin, end, [&](const auto& packet) {
        count += packet.transactionsCount();
    });

    return count;
}

size_t cs::ConveyerBase::packetsTableSize() const {
    cs::SharedLock lock(sharedMutex_);
    return pimpl_->packetsTable.size();
}

std::unique_lock<cs::SharedMutex> cs::ConveyerBase::lock() const {
    return std::unique_lock<cs::SharedMutex>(sharedMutex_);
}

void cs::ConveyerBase::flushTransactions() {
    cs::Lock lock(sharedMutex_);

    auto packets = pimpl_->packetQueue.pop();
    auto round = currentRoundNumber();

    for (auto& packet : packets) {
        if ((packet.transactionsCount() != 0u)) {
            // set max round for no setupped packets
            if (!packet.expiredRound()) {
                packet.setExpiredRound(round + cs::ConfigHolder::instance().config()->conveyerData().maxPacketLifeTime);
            }

            if (packet.isHashEmpty()) {
                if (!packet.makeHash()) {
                    cserror() << csname() << "Transaction packet hashing failed";
                    continue;
                }
            }

            if (packet.signatures().empty()) {
                if (!packet.sign(pimpl_->privateKey)) {
                    cswarning() << "Can not sign unsigned transaction packet, drop";
                    break;
                }
            }

            emit packetFlushed(packet);

            addPacketToMeta(packet);
        }
    }
}

void cs::ConveyerBase::onRoundChanged(cs::RoundNumber round) {
    cs::Packets expiredPackets;

    {
        cs::SharedLock lock(sharedMutex_);

        for (const auto& element : pimpl_->packetsTable) {
            if (element.second.expiredRound() < round) {
                expiredPackets.push_back(element.second);
            }
        }
    }

    if (expiredPackets.empty()) {
        return;
    }

    {
        cs::Lock lock(sharedMutex_);

        for (const auto& packet : expiredPackets) {
            const auto& hash = packet.hash();

            pimpl_->packetsTable.erase(hash);

            emit packetExpired(packet);
        }
    }
}

void cs::ConveyerBase::addPacketToMeta(cs::TransactionsPacket& packet) {
    auto hash = packet.hash();

    if (!isPacketAtCache(packet)) {
        pimpl_->packetsTable.emplace(std::move(hash), std::move(packet));
    }
    else {
        csdebug() << csname() << "Same transaction packet already in packet table " << hash.toString();
    }
}

void cs::ConveyerBase::changeRound(cs::RoundNumber round) {
    if (currentRoundNumber() != round) {
        pimpl_->currentRound = round;

        emit roundChanged(round);
    }
}

std::optional<cs::TransactionsPacket> cs::ConveyerBase::findPacketAtMeta(const cs::TransactionsPacketHash& hash) const {
    auto iter = pimpl_->packetsTable.find(hash);

    if (iter != pimpl_->packetsTable.end()) {
        return iter->second;
    }

    for (const auto& element : pimpl_->metaStorage) {
        auto metaIter = element.meta.hashTable.find(hash);

        if (metaIter != element.meta.hashTable.end()) {
            return metaIter->second;
        }
    }

    return std::nullopt;
}

void cs::ConveyerBase::removeHashesFromTable(const cs::PacketsHashes& hashes) {
    for (const auto& hash : hashes) {
        csdetails() << csname() << " remove hash " << hash.toString();
        pimpl_->packetsTable.erase(hash);
    }
}

cs::TransactionsPacketTable& cs::ConveyerBase::poolTable(cs::RoundNumber round) {
    cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

    if (!meta) {
        return pimpl_->packetsTable;
    }

    if (!meta->hashTable.empty()) {
        return meta->hashTable;
    }

    return pimpl_->packetsTable;
}

bool cs::ConveyerBase::isPacketAtCache(const cs::TransactionsPacket& packet) {
    auto hash = packet.hash();
    auto iter = pimpl_->packetsTable.find(hash);

    if (iter != pimpl_->packetsTable.end()) {
        return true;
    }

    for (const auto& element : pimpl_->metaStorage) {
        auto metaIter = element.meta.hashTable.find(hash);

        if (metaIter != element.meta.hashTable.end()) {
            return true;
        }
    }

    return false;
}

cs::Conveyer& cs::Conveyer::instance() {
    static cs::Conveyer conveyer;
    return conveyer;
}
