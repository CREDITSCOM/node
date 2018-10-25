#ifndef CONVEYER_H
#define CONVEYER_H

#include <csnode/nodecore.h>
#include <lib/system/common.hpp>
#include <lib/system/timer.hpp>

#include <memory>

class Node;

namespace cs
{
    class Solver;
}

namespace csdb
{
    class Transaction;
}

namespace cs
{
    ///
    /// @brief The Conveyer class, represents utils and mechanics
    /// to transfer packets of transactions, consensus helper
    ///
    class Conveyer
    {
    private:
        explicit Conveyer();
        ~Conveyer();

    public:
        ///
        /// @brief Instance of conveyer, singleton
        /// @return Returns static conveyer object pointer, Meyers singleton
        ///
        static Conveyer* instance();

        ///
        /// @brief Adds transaction to conveyer, start point of conveyer
        /// @param transaction csdb Transaction, not valid transavtion would not be
        /// sent to network
        ///
        void addTransaction(const csdb::Transaction& transaction);

    protected:

        /// try to send transactions to network
        void flushTransactions();

    private:

        /// pointer implementation
        struct Impl;
        std::unique_ptr<Impl> pimpl;

        /// sync
        cs::SharedMutex m_sharedMutex;

        /// sends transactions blocks to network
        cs::Timer m_sendingTimer;
    };
}

#endif // CONVEYER_H
