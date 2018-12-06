#include "csnode/conveyer.hpp"

#include <csdb/transaction.hpp>

#include <exception>
#include <iomanip>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

/// pointer implementation realization
struct cs::ConveyerBase::Impl {
  // first storage of transactions, before sending to network
  cs::TransactionsBlock transactionsBlock;

  // current round transactions packets storage
  cs::TransactionsPacketTable packetsTable;

  // main conveyer meta data
  cs::ConveyerMetaStorage metaStorage;

  // characteristic meta base
  cs::CharacteristicMetaStorage characteristicMetas;

  // cached active current round number
  std::atomic<cs::RoundNumber> currentRound = 0;

public signals:
  cs::PacketFlushSignal flushPacket;
};

cs::ConveyerBase::ConveyerBase() {
  pimpl_ = std::make_unique<cs::ConveyerBase::Impl>();
  pimpl_->metaStorage.append(cs::ConveyerMetaStorage::Element());
}

cs::ConveyerBase::~ConveyerBase() = default;

cs::PacketFlushSignal& cs::ConveyerBase::flushSignal() {
  return pimpl_->flushPacket;
}

void cs::ConveyerBase::addTransaction(const csdb::Transaction& transaction) {
  if (!transaction.is_valid()) {
    cswarning() << "CONVEYER> Can not add no valid transaction to conveyer";
    return;
  }

  cs::Lock lock(sharedMutex_);

  if (pimpl_->transactionsBlock.empty() || (pimpl_->transactionsBlock.back().transactionsCount() >= MaxPacketTransactions)) {
    pimpl_->transactionsBlock.push_back(cs::TransactionsPacket());
  }

  pimpl_->transactionsBlock.back().addTransaction(transaction);
}

void cs::ConveyerBase::addTransactionsPacket(const cs::TransactionsPacket& packet) {
  cs::TransactionsPacketHash hash = packet.hash();
  cs::Lock lock(sharedMutex_);

  if (auto iterator = pimpl_->packetsTable.find(hash); iterator == pimpl_->packetsTable.end()) {
    pimpl_->packetsTable.emplace(std::move(hash), packet);
  }
  else {
    cswarning() << "CONVEYER> Same hash already exists at table: " << hash.toString();
  }
}

const cs::TransactionsPacketTable& cs::ConveyerBase::transactionsPacketTable() const {
  return pimpl_->packetsTable;
}

const cs::TransactionsBlock& cs::ConveyerBase::transactionsBlock() const {
  return pimpl_->transactionsBlock;
}

std::optional<cs::TransactionsPacket> cs::ConveyerBase::createPacket() const {
  cs::ConveyerMeta* meta = pimpl_->metaStorage.get(currentRoundNumber());

  if (!meta) {
    cserror() << "CONVEYER> Can not create transactions packet";
    return std::nullopt;
  }

  cs::TransactionsPacket packet;
  cs::PacketsHashes& hashes = meta->roundTable.hashes;
  cs::TransactionsPacketTable& table = pimpl_->packetsTable;

  for (const auto& hash : hashes) {
    const auto iterator = table.find(hash);

    if (iterator == table.end()) {
      cserror() << "CONVEYER>: PACKET CREATION HASH NOT FOUND";
      return std::nullopt;
    }

    const auto& transactions = iterator->second.transactions();

    for (const auto& transaction : transactions) {
      if (!packet.addTransaction(transaction)) {
        cserror() << "Can not add transaction to packet in consensus";
      }
    }
  }

  return std::make_optional<cs::TransactionsPacket>(std::move(packet));
}

void cs::ConveyerBase::updateRoundTable(cs::RoundTable&& table) {
  cslog() << "CONVEYER> updateRoundTable";

  if (table.round != currentRoundNumber()) {
    cserror() << "CONVEYER> Update round table in conveyer failed: round mismatch";
    return;
  }

  {
    cs::Lock lock(sharedMutex_);
    cs::ConveyerMeta* meta = pimpl_->metaStorage.get(table.round);

    if (meta == nullptr) {
      cserror() << "CONVEYER> Update round table in conveyer failed: round table not found, call setRound() before";
      return;
    }

    meta->roundTable.general = std::move(table.general);
    meta->roundTable.confidants = std::move(table.confidants);
  }
}

void cs::ConveyerBase::setRound(cs::RoundTable&& table) {
  cslog() << "CONVEYER> SetRound";

  if (table.round <= currentRoundNumber()) {
    cserror() << "CONVEYER> Setting round in conveyer failed";
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

  csdebug() << "CONVEYER> Needed round hashes count " << neededHashes.size();

  for (const auto& hash : neededHashes) {
    csdetails() << "CONVEYER> Need hash > " << hash.toString();
  }

  {
    cs::Lock lock(sharedMutex_);
    pimpl_->currentRound = table.round;
  }

  cs::ConveyerMetaStorage::Element element;
  element.round = table.round;
  element.meta.neededHashes = std::move(neededHashes);
  element.meta.roundTable = std::move(table);

  {
    cs::Lock lock(sharedMutex_);

    if (!pimpl_->metaStorage.contains(pimpl_->currentRound)) {
      pimpl_->metaStorage.append(std::move(element));
    }
    else {
      csfatal() << "CONVEYER> Meta round currently in conveyer";
    }
  }

  csdebug() << "CONVEYER> Current table size " << pimpl_->packetsTable.size();
}

const cs::RoundTable& cs::ConveyerBase::currentRoundTable() const {
  cs::ConveyerMeta* meta = pimpl_->metaStorage.get(pimpl_->currentRound);
  return meta->roundTable;
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
    cserror() << __func__ << ", index " << index << "out of range , confidants count " << confidantsReference.size()
              << ", on round " << pimpl_->currentRound;
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

const cs::PacketsHashes& cs::ConveyerBase::currentNeededHashes() const {
  return *(neededHashes(currentRoundNumber()));
}

const cs::PacketsHashes* cs::ConveyerBase::neededHashes(cs::RoundNumber round) const {
  cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

  if (!meta) {
    cserror() << "CONVEYER: Bad needed hashes, check node logic";
    return nullptr;
  }

  return &(meta->neededHashes);
}

void cs::ConveyerBase::addFoundPacket(cs::RoundNumber round, cs::TransactionsPacket&& packet) {
  cs::Lock lock(sharedMutex_);

  cs::ConveyerMeta* metaPointer = pimpl_->metaStorage.get(round);
  cs::TransactionsPacketTable* tablePointer = nullptr;

  if (metaPointer == nullptr) {
    cserror() << "CONVEYER> Can not add sync packet because meta pointer do not exist";
    return;
  }

  tablePointer = (round == pimpl_->currentRound) ? &pimpl_->packetsTable : &metaPointer->hashTable;

  if (tablePointer == nullptr) {
    cserror() << "CONVEYER> Can not add sync packet because table pointer do not exist";
    return;
  }

  cs::PacketsHashes& hashes = metaPointer->neededHashes;

  if (auto iterator = std::find(hashes.begin(), hashes.end(), packet.hash()); iterator != hashes.end()) {
    csdebug() << "CONVEYER> Adding synced packet";
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
    cserror() << "CONVEYER> Needed hashes of " << round << " round not found";
    return true;
  }

  return meta->neededHashes.empty();
}

const cs::Notifications& cs::ConveyerBase::notifications() const {
  cs::ConveyerMeta* meta = pimpl_->metaStorage.get(currentRoundNumber());
  return meta->notifications;
}

void cs::ConveyerBase::addNotification(const cs::Bytes& bytes) {
  cs::ConveyerMeta* meta = pimpl_->metaStorage.get(currentRoundNumber());

  if (meta) {
    csdebug() << "CONVEYER> Writer notification added";
    meta->notifications.push_back(bytes);
  }
}

std::size_t cs::ConveyerBase::neededNotificationsCount() const {
  cs::ConveyerMeta* meta = pimpl_->metaStorage.get(pimpl_->currentRound);

  // TODO: check if +1 is correct
  if (meta) {
    return (meta->roundTable.confidants.size() / 2) + 1;
  }

  csdebug() << "CONVEYER> No notifications at current round";
  return 0;
}

bool cs::ConveyerBase::isEnoughNotifications(cs::ConveyerBase::NotificationState state) const {
  cs::SharedLock lock(sharedMutex_);

  const std::size_t neededConfidantsCount = neededNotificationsCount();
  const std::size_t notificationsCount = notifications().size();

  cslog() << "CONVEYER> Current notifications count - " << notificationsCount;
  cslog() << "CONVEYER> Needed confidans count - " << neededConfidantsCount;

  if (state == NotificationState::Equal) {
    return notificationsCount == neededConfidantsCount;
  }

  return notificationsCount >= neededConfidantsCount;
}

void cs::ConveyerBase::addCharacteristicMeta(RoundNumber round, CharacteristicMeta&& characteristic) {
  if (!pimpl_->characteristicMetas.contains(round)) {
    cs::CharacteristicMetaStorage::Element metaElement;
    metaElement.meta = std::move(characteristic);
    metaElement.round = round;

    pimpl_->characteristicMetas.append(std::move(metaElement));
  }
  else {
    csdebug() << "CONVEYER> Received meta is currently in meta stack";
  }
}

std::optional<cs::CharacteristicMeta> cs::ConveyerBase::characteristicMeta(const cs::RoundNumber round) {
  if (!pimpl_->characteristicMetas.contains(round)) {
    csdebug() << "CONVEYER> Characteristic meta not received";
    return std::nullopt;
  }

  auto meta = pimpl_->characteristicMetas.extract(round);
  return std::make_optional<cs::CharacteristicMeta>(std::move(meta).value());
}

void cs::ConveyerBase::setCharacteristic(const Characteristic& characteristic, cs::RoundNumber round) {
  cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

  if (meta) {
    csdebug() << "CONVEYER> Characteristic set to conveyer, #" << round;
    meta->characteristic = characteristic;
  }
}

const cs::Characteristic* cs::ConveyerBase::characteristic(cs::RoundNumber round) const {
  auto meta = pimpl_->metaStorage.get(round);

  if (!meta) {
    cserror() << "CONVEYER> Get characteristic, logic error, can not find characteristic, #" << round;
    return nullptr;
  }

  return &meta->characteristic;
}

cs::Hash cs::ConveyerBase::characteristicHash(cs::RoundNumber round) const {
  const Characteristic* pointer = characteristic(round);

  if (!pointer) {
    cserror() << "CONVEYER> Null pointer of characteristic, return empty Hash, #" << round;
    return cs::Hash();
  }

  return generateHash(pointer->mask.data(), pointer->mask.size());
}

std::optional<csdb::Pool> cs::ConveyerBase::applyCharacteristic(const cs::PoolMetaInfo& metaPoolInfo, const cs::PublicKey& sender) {
  cs::RoundNumber round = static_cast<cs::RoundNumber>(metaPoolInfo.sequenceNumber);
  csprint() << ", round " << round;

  cs::Lock lock(sharedMutex_);
  cs::ConveyerMeta* meta = pimpl_->metaStorage.get(round);

  if (!meta) {
    cserror() << "CONVEYER> Apply characteristic failed, no meta in meta storage";
    return std::nullopt;
  }

  cs::TransactionsPacketTable hashTable;
  const cs::PacketsHashes& localHashes = meta->roundTable.hashes;
  const cs::Characteristic& characteristic = meta->characteristic;
  cs::TransactionsPacketTable& currentHashTable = pimpl_->packetsTable;

  cslog() << "CONVEYER> ApplyCharacteristic, characteristic bytes size " << characteristic.mask.size();
  csdebug() << "CONVEYER> ApplyCharacteristic, viewing hashes count " << localHashes.size();
  csdebug() << "CONVEYER> ApplyCharacteristic, viewing hash table size " << currentHashTable.size();

  csdb::Pool newPool;
  std::size_t maskIndex = 0;
  const cs::Bytes& mask = characteristic.mask;
  cs::TransactionsPacket invalidTransactions;

  for (const auto& hash : localHashes) {
    // try to get from meta if can
    auto optionalPacket = findPacket(hash, round);

    if (!optionalPacket.has_value()) {
      cserror() << "CONVEYER> ApplyCharacteristic: HASH NOT FOUND " << hash.toString();
      removeHashesFromTable(localHashes);
      return std::nullopt;
    }

    auto packet = std::move(optionalPacket).value();
    const auto& transactions = packet.transactions();

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
      cserror() << "CONVEYER: Apply characteristic hash failed, mask size: " << mask.size() << " mask index: " << maskIndex;
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
    cslog() << "\tCharacteristic size: " << characteristic.mask.size() << ", new pool transactions count: " << newPool.transactions_count();
    cswarning() << "\tSome of transactions is not valid";
  }

  cslog() << "\tsequence = " << metaPoolInfo.sequenceNumber;

  newPool.set_sequence(metaPoolInfo.sequenceNumber);
  newPool.add_user_field(0, metaPoolInfo.timestamp);

  csdb::internal::byte_array writerPublicKey(sender.begin(), sender.end());
  newPool.set_writer_public_key(std::move(writerPublicKey));

  csprint() << "done";
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

size_t cs::ConveyerBase::blockTransactionsCount() const {
  cs::SharedLock lock(sharedMutex_);
  size_t count = 0;

  std::for_each(pimpl_->transactionsBlock.begin(), pimpl_->transactionsBlock.end(), [&](const auto& block) {
    count += block.transactionsCount();
  });

  return count;
}

cs::SharedMutex& cs::ConveyerBase::sharedMutex() const {
  return sharedMutex_;
}

void cs::ConveyerBase::flushTransactions() {
  cs::Lock lock(sharedMutex_);
  std::size_t allTransactionsCount = 0;

  for (auto& packet : pimpl_->transactionsBlock) {
    const std::size_t transactionsCount = packet.transactionsCount();

    if ((transactionsCount != 0u) && packet.isHashEmpty()) {
      packet.makeHash();

      // try to send save in node
      pimpl_->flushPacket(packet);

      auto hash = packet.hash();

      if (hash.isEmpty()) {
        cserror() << "CONVEYER > Transaction packet hashing failed";
      }

      if (pimpl_->packetsTable.count(hash) == 0u) {
        pimpl_->packetsTable.emplace(std::move(hash), std::move(packet));
      }
      else {
        cserror() << "CONVEYER > Logical error, adding transactions packet more than one time";
      }

      allTransactionsCount += transactionsCount;
    }
  }

  if (!pimpl_->transactionsBlock.empty()) {
    pimpl_->transactionsBlock.clear();
  }
}

void cs::ConveyerBase::removeHashesFromTable(const cs::PacketsHashes& hashes) {
  for (const auto& hash : hashes) {
    pimpl_->packetsTable.erase(hash);
  }
}

cs::Conveyer& cs::Conveyer::instance() {
  static cs::Conveyer conveyer;
  return conveyer;
}
