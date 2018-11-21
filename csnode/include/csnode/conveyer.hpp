#ifndef CONVEYER_HPP
#define CONVEYER_HPP

#include <csnode/nodecore.hpp>

#include <lib/system/common.hpp>
#include <lib/system/signals.hpp>

#include <memory>
#include <optional>

namespace csdb
{
    class Transaction;
}

namespace cs
{
    using PacketFlushSignal = cs::Signal<void(const cs::TransactionsPacket&)>;

    ///
    /// @brief The Conveyer class, represents utils and mechanics
    /// to transfer packets of transactions, consensus helper.
    ///
    class ConveyerBase
    {
    protected:
        ConveyerBase();
        ~ConveyerBase();

    public:
        enum class NotificationState {
            Equal,
            GreaterEqual
        };

        enum : unsigned int {
            HashTablesStorageCapacity = 5,
            CharacteristicMetaCapacity = HashTablesStorageCapacity,
        };

        ///
        /// @brief Returns transactions packet flush signal.
        /// Generates when transactions packet should be sent to network.
        ///
        cs::PacketFlushSignal& flushSignal();

        ///
        /// @brief Adds transaction to conveyer, start point of conveyer.
        /// @param transaction csdb Transaction, not valid transavtion would not be
        /// sent to network.
        ///
        void addTransaction(const csdb::Transaction& transaction);

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
        /// @brief Returns transactions block, first stage of conveyer.
        ///
        const cs::TransactionsBlock& transactionsBlock() const;

        ///
        /// @brief Returns transactions packet created in current round.
        /// @warning Slow-performance method. No thread safe.
        ///
        std::optional<cs::TransactionsPacket> createPacket() const;

        // round info

        ///
        /// @brief Starts round of conveyer, checks all transactions packet hashes
        /// at round table.
        /// @param table Current blockchain round table.
        ///
        void setRound(cs::RoundTable&& table);

        ///
        /// @brief Returns current blockchain round table.
        ///
        const cs::RoundTable& currentRoundTable() const;

        ///
        /// @brief Returns blockchain round table of Round key.
        /// @warning If round table does not exist in meta, returns nullptr.
        ///
        const cs::RoundTable* roundTable(cs::RoundNumber round) const;

        ///
        /// @brief Returns current round number.
        /// Locks mutex and returns safe round number.
        ///
        cs::RoundNumber currentRoundNumber() const;

        ///
        /// @brief Returns current round needed hashes.
        ///
        const cs::Hashes& currentNeededHashes() const;

        ///
        /// @brief Returns round needed hashes.
        /// @return returns cs::Hashes. If no hashes found returns nullptr.
        ///
        const cs::Hashes* neededHashes(cs::RoundNumber round) const;

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
        /// @brief Returns calcualted characteristic hash by blake2.
        ///
        cs::Hash characteristicHash(cs::RoundNumber round) const;

        ///
        /// @brief Applyies current round characteristic to create csdb::Pool.
        /// @param metaPoolInfo pool meta information.
        /// @param sender Sender public key.
        /// @return pool Returns created csdb::Pool, otherwise returns nothing.
        ///
        std::optional<csdb::Pool> applyCharacteristic(const cs::PoolMetaInfo& metaPoolInfo, const cs::PublicKey& sender = cs::PublicKey());

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

        // sync, try do not use it :]

        ///
        /// @brief Returns shared mutex object reference to lock/unlock outside conveyer behaviour.
        ///
        cs::SharedMutex& sharedMutex() const;

    public slots:

        /// try to send transactions packets to network
        void flushTransactions();

    private:

        /// pointer implementation
        struct Impl;
        std::unique_ptr<Impl> pimpl;

        mutable cs::SharedMutex m_sharedMutex;
    };

    class Conveyer : public ConveyerBase {
    public:
        ///
        /// @brief Instance of conveyer, singleton.
        /// @return Returns static conveyer object reference, Meyers singleton.
        ///
        static Conveyer& instance();

    private:
        Conveyer() : ConveyerBase() {}
        ~Conveyer() = default;
    };
}

#endif // CONVEYER_HPP
