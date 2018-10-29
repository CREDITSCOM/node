#ifndef CONVEYER_H
#define CONVEYER_H

#include <csnode/nodecore.h>

#include <lib/system/common.hpp>
#include <lib/system/timer.hpp>
#include <lib/system/signals.hpp>

#include <memory>
#include <optional>

class Node;

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
    class Conveyer
    {
    public:
        explicit Conveyer();
        Conveyer (const Conveyer&) = delete;
        Conveyer& operator=(const Conveyer&) = delete;
        Conveyer (Conveyer&&) = delete;
        Conveyer& operator=(Conveyer&&) = delete;

        ~Conveyer();

    public:
        enum class NotificationState {
            Equal,
            GreaterEqual
        };

        enum : unsigned int {
            HashTablesStorageCapacity = 10,
            CharacteristicMetaCapacity = HashTablesStorageCapacity
        };

        ///
        /// @brief Instance of conveyer, singleton.
        /// @return Returns static conveyer object reference, Meyers singleton.
        ///
        static Conveyer& instance();

        ///
        /// @brief Sets node pointer to get info about node level.
        ///
        void setNode(Node* node);

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
        const cs::TransactionsPacketHashTable& transactionsPacketTable() const;

        ///
        /// @brief Returns transactions block, first stage of conveyer.
        ///
        const cs::TransactionsBlock& transactionsBlock() const;

        ///
        /// @brief Returns transactions packet by hash.
        /// Search in current hash table
        ///
        const cs::TransactionsPacket& packet(const cs::TransactionsPacketHash& hash) const;

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
        const cs::RoundTable& roundTable() const;

        ///
        /// @brief Returns current round number.
        /// Locks mutex and returns safe round number.
        ///
         cs::RoundNumber roundNumber() const;

        ///
        /// @brief Returns safe copy of round table.
        /// Locks mutex and returns deep copy of round table.
        ///
        const cs::RoundTable roundTableSafe() const;

        ///
        /// @brief Returns current round needed hashes.
        ///
        const cs::Hashes& neededHashes() const;

        ///
        /// @brief Adds synced packet to conveyer.
        ///
        void addFoundPacket(cs::TransactionsPacket&& packet);

        ///
        /// @brief Returns state of current round hashes.
        /// Checks conveyer needed round hashes on empty state.
        ///
        bool isSyncCompleted() const;

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
        void addCharacteristicMeta(const cs::CharacteristicMeta& meta);

        ///
        /// @brief Returns characteristic meta from storage if found otherwise return empty meta.
        /// @param round Current blockchain round.
        ///
        std::optional<cs::CharacteristicMeta> characteristicMeta(const cs::RoundNumber round);

        // characteristic

        ///
        /// @brief Sets current round characteristic function.
        /// @param characteristic Created characteristic on network level.
        ///
        void setCharacteristic(const cs::Characteristic& characteristic);

        ///
        /// @brief Returns current round characteristic.
        ///
        const cs::Characteristic& characteristic() const;

        ///
        /// @brief Returns calcualted characteristic hash by blake2.
        ///
        cs::Hash characteristicHash() const;

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
        ///
        std::optional<cs::TransactionsPacket> searchPacket(const cs::TransactionsPacketHash& hash, const cs::RoundNumber round) const;

        // sync, try do not use it :]

        ///
        /// @brief Returns shared mutex object reference to lock/unlock outside conveyer behaviour.
        ///
        cs::SharedMutex& sharedMutex() const;

    protected:

        /// try to send transactions packets to network
        void flushTransactions();

    private:

        /// pointer implementation
        struct Impl;
        std::unique_ptr<Impl> pimpl;

        mutable cs::SharedMutex m_sharedMutex;

        /// sends transactions blocks to network
        cs::Timer m_sendingTimer;
    };
}

#endif // CONVEYER_H
