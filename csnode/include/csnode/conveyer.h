#ifndef CONVEYER_H
#define CONVEYER_H

#include <csnode/nodecore.h>

#include <lib/system/common.hpp>
#include <lib/system/timer.hpp>
#include <lib/system/signals.hpp>

#include <memory>

namespace slv2
{
    class SolverCore;
}

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
    private:
        explicit Conveyer();
        ~Conveyer();

    public:
        enum class NotificationState {
            Equal,
            GreaterEqual
        };

        enum : unsigned int {
            HashTablesStorageCapacity = 10
        };

        ///
        /// @brief Instance of conveyer, singleton.
        /// @return Returns static conveyer object pointer, Meyers singleton.
        ///
        static Conveyer* instance();

        ///
        /// @brief Sets solver pointer to get info about rounds and consensus.
        ///
        void setSolver(slv2::SolverCore* solver);

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
        /// @brief Starts round of conveyer, checks all transactions packet hashes
        /// at round table.
        /// @param table Current blockchain round table.
        ///
        void newRound(const cs::RoundTable& table);

        ///
        /// @brief Returns current round needed hashes.
        ///
        const cs::Hashes& neededHashes() const;

        ///
        /// @brief Returns state of current round hashes.
        /// Checks conveyer needed round hashes on empty state.
        ///
        bool isSyncCompleted() const;

        // notifications

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
        /// @param state Check state of notifications
        ///
        bool isEnoughNotifications(NotificationState state) const;

    protected:

        /// try to send transactions packets to network
        void flushTransactions();

    private:

        /// pointer implementation
        struct Impl;
        std::unique_ptr<Impl> pimpl;

        /// sync
        mutable cs::SharedMutex m_sharedMutex;

        /// sends transactions blocks to network
        cs::Timer m_sendingTimer;
    };
}

#endif // CONVEYER_H
