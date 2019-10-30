#ifndef CONVEYER_HPP
#define CONVEYER_HPP

#include <csnode/nodecore.hpp>
#include <csnode/packetqueue.hpp>

#include <lib/system/common.hpp>
#include <lib/system/signals.hpp>

#include <memory>
#include <optional>

class Config;
struct ConveyerData;

namespace csdb {
class Transaction;
}

namespace cs {
using PacketFlushSignal = cs::Signal<void(const cs::TransactionsPacket&)>;
using StatesSignal = cs::Signal<void(const std::vector<csdb::Transaction>&)>;
using RoundChangeSignal = cs::Signal<void(cs::RoundNumber)>;

///
/// @brief The Conveyer class, represents utils and mechanics
/// to transfer packets of transactions, consensus helper.
///
class ConveyerBase {
protected:
    ConveyerBase();
    ~ConveyerBase();

public:
    enum class NotificationState {
        Equal,
        GreaterEqual
    };

    enum : unsigned int {
        HashTablesStorageCapacity = 100, // equals to Consensus::MaxRoundsCancelContract to strongly prevent duplicated new_state transactions
        MetaCapacity = HashTablesStorageCapacity,

        // queue
        MaxPacketTransactions = 100,
        MaxPacketsPerRound = 10,
        MaxQueueSize = 1000000
    };

    void setPrivateKey(const cs::PrivateKey& privateKey);

    ///
    /// @brief Sets cached conveyer round number for utility.
    /// @warning Call this method before setTable method.
    ///
    void setRound(cs::RoundNumber round);

    ///
    /// @brief Adds transaction to conveyer, start point of conveyer.
    /// @param transaction csdb Transaction, not valid transavtion would not be
    /// sent to network.
    ///
    void addTransaction(const csdb::Transaction& transaction);

    ///
    /// @brief Adds packet to transactions block as monolith entity.
    /// @param packet Created from outside packet with transactions.
    ///
    void addSeparatePacket(const cs::TransactionsPacket& packet);

    ///
    /// @brief Adds transactions packet received by network.
    /// @param packet Created from network transactions packet.
    ///
    void addTransactionsPacket(const cs::TransactionsPacket& packet);

    ///
    /// @brief Returns current round transactions packet hash table.
    ///
    const cs::TransactionsPacketTable& transactionsPacketTable() const;

    ///
    /// @brief Returns transactions packet queue, first stage of conveyer.
    ///
    const cs::PacketQueue& packetQueue() const;

    ///
    /// @brief Returns pair of transactions packet created in current round and smart contract packets.
    /// @warning Slow-performance method. Thread safe.
    ///
    std::optional<std::pair<cs::TransactionsPacket, cs::Packets>> createPacket(cs::RoundNumber rNum) const;

    // round info

    ///
    /// @brief Starts round of conveyer, checks all transactions packet hashes.
    /// at round table.
    /// @param table Current blockchain round table.
    /// @warning Call this method after setRound.
    ///
    void setTable(const cs::RoundTable& table);

    ///
    /// @fn void ConveyerBase::updateRoundTable(const cs::RoundTable& table).
    ///
    /// @brief Updates the round table described by table.
    ///
    /// @author Alexander Avramenko.
    /// @date  29.11.2018.
    ///
    /// @param cachedRound Remove conveyer meta storage to this point.
    /// @param [in,out]    table   The new round table contains trusted nodes and round number.
    /// @warning all meta in conveyer will be removed from cached round to table round number.
    ///
    void updateRoundTable(cs::RoundNumber cachedRound, const RoundTable& table);

    ///
    /// @brief Returns current blockchain round table.
    ///
    const cs::RoundTable& currentRoundTable() const;

    // confidants helpers

    ///
    /// @brief Returns current round confidants keys.
    ///
    const cs::ConfidantsKeys& confidants() const;

    ///
    /// @brief Returns current round confidants keys count.
    ///
    size_t confidantsCount() const;

    ///
    /// @brief Returns existing state of confidant by index at current round.
    ///
    bool isConfidantExists(size_t index) const;

    ///
    /// @brief Returns existing state of confidant by his public key.
    ///
    bool isConfidantExists(const cs::PublicKey& confidant) const;

    ///
    /// @brief Returns confidant key at current round table by index.
    /// @warning call isConfidantExits before using this method.
    ///
    const cs::PublicKey& confidantByIndex(size_t index) const;

    ///
    /// @brief Returns confidant public key if confidant exists in round table.
    /// @param index. Index of condifant.
    /// @warning Returns copy of public key.
    ///
    std::optional<cs::PublicKey> confidantIfExists(size_t index) const;

    // round information interfaces

    ///
    /// @brief Returns blockchain round table of Round key.
    /// @warning If round table does not exist in meta, returns nullptr.
    ///
    const cs::RoundTable* roundTable(cs::RoundNumber round) const;

    ///
    /// @brief Returns current round number.
    /// Returns copy of atomic round number.
    ///
    cs::RoundNumber currentRoundNumber() const;

    ///
    /// @brief Returns previous round number (special for characteristic and pool creation).
    ///
    cs::RoundNumber previousRoundNumber() const;

    ///
    /// @brief Returns current round needed hashes.
    ///
    const cs::PacketsHashes& currentNeededHashes() const;

    ///
    /// @brief Returns round needed hashes.
    /// @return returns cs::Hashes. If no hashes found returns nullptr.
    ///
    const cs::PacketsHashes* neededHashes(cs::RoundNumber round) const;

    ///
    /// @brief Adds synced packet to conveyer.
    ///
    void addFoundPacket(cs::RoundNumber round, cs::TransactionsPacket&& packet);

    ///
    /// @brief Returns state of current round hashes sync.
    /// Checks conveyer needed round hashes on empty state.
    ///
    bool isSyncCompleted() const;

    ///
    /// @brief Returns state of arg round hashes sync.
    ///
    bool isSyncCompleted(cs::RoundNumber round) const;

    // writer notifications

    ///
    /// @brief Returns confidants notifications to writer.
    ///
    const cs::Notifications& notifications() const;

    ///
    /// @brief Adds writer notification in bytes representation.
    /// @param bytes Received from network notification bytes.
    ///
    void addNotification(const cs::Bytes& bytes);

    ///
    /// @brief Returns count of needed writer notifications.
    ///
    std::size_t neededNotificationsCount() const;

    ///
    /// @brief Returns current notifications check of needed count.
    /// @param state Check state of notifications.
    ///
    bool isEnoughNotifications(NotificationState state) const;

    // characteristic meta

    ///
    /// @brief Adds characteristic meta if early characteristic recevied from network.
    /// @param meta Created on network characteristic meta information.
    ///
    void addCharacteristicMeta(cs::RoundNumber round, CharacteristicMeta&& characteristic);

    ///
    /// @brief Returns characteristic meta from storage if found otherwise return empty meta.
    /// @param round Current blockchain round.
    ///
    std::optional<cs::CharacteristicMeta> characteristicMeta(cs::RoundNumber round);

    // characteristic

    ///
    /// @brief Sets round characteristic function.
    /// @param characteristic Created characteristic on network level.
    ///
    void setCharacteristic(const Characteristic& characteristic, cs::RoundNumber round);

    ///
    /// @brief Returns current round characteristic.
    ///
    const cs::Characteristic* characteristic(cs::RoundNumber round) const;

    ///
    /// @brief Returns calcualted characteristic hash.
    ///
    cs::Hash characteristicHash(cs::RoundNumber round) const;

    ///
    /// @brief Applyies current round characteristic to create csdb::Pool.
    /// @param metaPoolInfo pool meta information.
    /// @param sender Sender public key.
    /// @return pool Returns created csdb::Pool, otherwise returns nothing.
    ///
    std::optional<csdb::Pool> applyCharacteristic(const cs::PoolMetaInfo& metaPoolInfo);

    // hash table storage

    ///
    /// @brief Searches transactions packet in current hash table, or in hash table storage.
    /// @param hash Created transactions packet hash.
    /// @return Returns transactions packet if its found, otherwise returns nothing.
    /// @warning No thread safe.
    ///
    std::optional<cs::TransactionsPacket> findPacket(const cs::TransactionsPacketHash& hash, const cs::RoundNumber round) const;

    ///
    /// @brief Returns existing of invalid transaction in meta storage.
    /// @param innerId of transaction to search equal transaction.
    /// @warning thread safe method.
    ///
    bool isMetaTransactionInvalid(int64_t id);

    ///
    /// @brief Returns summary block (first stage) transactions count that
    /// does not flushed to network. Thread safe method.
    ///
    size_t packetQueueTransactionsCount() const;

    ///
    /// @brief Returns current send cache size
    ///
    size_t sendCacheCount() const;

    // sync, try do not use it :]
    std::unique_lock<cs::SharedMutex> lock() const;

    ///
    /// @brief Adds transactions packet hash to send cache, key will be current round.
    /// @param hash, Rejected from consensus.
    /// @return returns true, if hash does not exist at send cache and exists at hash table.
    ///  returns false if hash exists at send cache or does not found at packets table.
    ///
    bool addRejectedHashToCache(const cs::TransactionsPacketHash& hash);

public signals:
    cs::PacketFlushSignal packetFlushed;
    cs::StatesSignal statesCreated;
    cs::RoundChangeSignal roundChanged;

public slots:

    /// try to send transactions packets to network
    void flushTransactions();

protected:
    void changeRound(cs::RoundNumber round);

    // searches transactions packet at all conveyer cache
    std::optional<cs::TransactionsPacket> findPacketAtMeta(const cs::TransactionsPacketHash& hash) const;

    void removeHashesFromTable(const cs::PacketsHashes& hashes);
    cs::TransactionsPacketTable& poolTable(cs::RoundNumber round);

    // returns true if packet is found at cache, otherwise - false
    bool isPacketAtCache(const cs::TransactionsPacket& packet);

    // returns true if hash is found at send cache, otherwise - false
    bool isHashAtSendCache(cs::RoundNumber round, const cs::TransactionsPacketHash& hash);
    bool isHashAtSendCache(const cs::TransactionsPacketHash& hash);

    // checks send cache to resend hashes if they still exists
    void checkSendCache();

    // remove this hash from send cache
    void removeHashFromSendCache(const cs::TransactionsPacketHash& hash);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;

    mutable cs::SharedMutex sharedMutex_;
};

class Conveyer : public ConveyerBase {
public:
    ///
    /// @brief Instance of conveyer, singleton.
    /// @return Returns static conveyer object reference, Meyers singleton.
    ///
    static Conveyer& instance();

private:
    Conveyer()
    : ConveyerBase() {
    }

    ~Conveyer() = default;
};
}  // namespace cs

#endif  // CONVEYER_HPP
