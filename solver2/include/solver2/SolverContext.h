#pragma once

#include "INodeState.h"
#include "SolverCore.h"

#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
#endif

class CallsQueueScheduler;
class Node;
namespace Credits
{
    class Solver;
    class Generals;
}

namespace slv2
{

    class SolverCore;
    using KeyType = csdb::internal::byte_array;

    // "Интерфейсный" класс для обращений из классов состояний к ядру солвера,
    // определяет подмножество вызовов солвера, которые доступны из классов состояний,
    // д. б. достаточным, но не избыточным одновременно
    class SolverContext
    {
    public:
        SolverContext() = delete;

        explicit SolverContext(SolverCore& core)
            : core(core)
        {}

        // Switch state methods:

        /// <summary>   Request to become normal node. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>

        inline void become_normal();

        /// <summary>   Request to become trusted node. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>

        inline void become_trusted();

        /// <summary>   Request to become writer node. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>

        inline void become_writer();

        /**
         * @fn  inline void SolverContext::become_collector();
         *
         * @brief   Request to become collector (main node)
         *
         * @author  aae
         * @date    01.10.2018
         */

        inline void become_collector();

        /// <summary>   Inform that receive enough vectors. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        
        inline void vectors_completed();

        /// <summary>   Inform that receive enough matrices. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>

        inline void matrices_completed();

        /// <summary>   Spawn next round. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>

        void spawn_next_round();

        // Fast access methods, may be removed at the end

        /// <summary>   Gets the node instance. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   A reference to a Node. </returns>

        inline Node& node() const;

        /// <summary>   Gets the generals instance. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   A reference to the Credits::Generals. </returns>

        inline Credits::Generals& generals() const;

        /// <summary>   Gets the scheduler instance. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   A reference to a CallsQueueScheduler. </returns>

        inline CallsQueueScheduler& scheduler() const;

        // Access to common state properties
        
        /// <summary>   Public key. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   A reference to a const KeyType public key. </returns>

        inline const KeyType& public_key() const;

        /// <summary>   Private key. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   A reference to a const KeyType private key. </returns>

        inline const KeyType& private_key() const;

        /// <summary>   Current hash vector. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   A reference to a const Credits::HashVector. </returns>

        inline const Credits::HashVector& hash_vector() const;

        /// <summary>   Gets the current round number. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   An int32_t. </returns>

        inline int32_t round() const;

        /// <summary>   Gets the own number among confidant (trusted) nodes. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   An uint8_t. </returns>

        uint8_t own_conf_number() const;

        /// <summary>   Gets count of trusted nodes in current round. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   The total number of trusted. </returns>

        size_t cnt_trusted() const;

        /// <summary>   Query if this node is in spammer mode. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   True if spammer, false if not. </returns>

        inline bool is_spammer() const;

        // Common operations, candidates for refactoring:
         
        
        /// <summary>   Makes a block from inner pool of collected and validated transactions and send it</summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>

        inline void make_and_send_block();

        /// <summary>   Adds transaction to inner list </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="tr">   The tr to add. </param>

        inline void add(const csdb::Transaction& tr);

        /// <summary>   Sends the transactions in inner list </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>

        inline void flush_transactions();

        /// <summary>   Verifies the given transaction </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="tr">   The tr. </param>
        ///
        /// <returns>   True if it succeeds, false if it fails. </returns>

        inline bool verify(const csdb::Transaction& tr) const;

        /// <summary>   Query if is vector received from passed sender </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="sender">   The sender. </param>
        ///
        /// <returns>   True if vect receive from, false if not. </returns>

        inline bool is_vect_recv_from(uint8_t sender) const;

        /// <summary>   Inform core to remember that vector from passed sender is received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="sender">   The sender. </param>

        inline void recv_vect_from(uint8_t sender);

        /// <summary>   Count of vectors received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   The total number of vect receive. </returns>

        inline size_t cnt_vect_recv() const;

        /// <summary>   Query if is matrix received from passed sender </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="sender">   The sender. </param>
        ///
        /// <returns>   True if matr receive from, false if not. </returns>

        inline bool is_matr_recv_from(uint8_t sender) const;

        /// <summary>   Inform core to remember that matrix from passed sender is received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="sender">   The sender. </param>

        inline void recv_matr_from(uint8_t sender);

        /// <summary>   Count of matrices received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   The total number of matr receive. </returns>

        inline size_t cnt_matr_recv() const;

        /// <summary>   Query if is hash received from passed sender </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="sender">   The sender. </param>
        ///
        /// <returns>   True if hash receive from, false if not. </returns>

        inline bool is_hash_recv_from(const PublicKey& sender) const;

        /// <summary>   Inform core to remember that hash from passed sender is received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="sender">   The sender. </param>

        inline void recv_hash_from(const PublicKey& sender);

        /// <summary>   Count of hashes received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   The total number of hash receive. </returns>

        inline size_t cnt_hash_recv() const;

    private:
        SolverCore& core;
    };

    Node& SolverContext::node() const
    {
        return *core.pnode;
    }

    Credits::Generals& SolverContext::generals() const
    {
        return *core.pgen;
    }

    CallsQueueScheduler& SolverContext::scheduler() const
    {
        return core.scheduler;
    }

    const KeyType& SolverContext::public_key() const
    {
        return core.public_key;
    }

    const KeyType& SolverContext::private_key() const
    {
        return core.private_key;
    }

    const Credits::HashVector& SolverContext::hash_vector() const
    {
        return core.getMyVector();
    }

    int32_t SolverContext::round() const
    {
        return core.cur_round;
    }

    bool SolverContext::is_spammer() const
    {
        return core.opt_spammer_on;
    }

    void SolverContext::become_normal()
    {
        core.handleTransitions(SolverCore::Event::SetNormal);
    }

    void SolverContext::become_trusted()
    {
        core.handleTransitions(SolverCore::Event::SetTrusted);
    }

    void SolverContext::become_writer()
    {
        core.handleTransitions(SolverCore::Event::SetWriter);
    }

    void SolverContext::become_collector()
    {
        core.handleTransitions(SolverCore::Event::SetCollector);
    }

    void SolverContext::vectors_completed()
    {
        core.handleTransitions(SolverCore::Event::Vectors);
    }

    void SolverContext::matrices_completed()
    {
        core.handleTransitions(SolverCore::Event::Matrices);
    }

    void SolverContext::make_and_send_block()
    {
        core.sendCurrentBlock();
    }

    void SolverContext::add(const csdb::Transaction& tr)
    {
        core.send_wallet_transaction(tr);
    }

    void SolverContext::flush_transactions()
    {
        core.flushTransactions();
    }

    bool SolverContext::verify(const csdb::Transaction& tr) const
    {
        return core.verify_signature(tr);
    }

    bool SolverContext::is_vect_recv_from(uint8_t sender) const
    {
        return core.recv_vect.find(sender) != core.recv_vect.cend();
    }

    void SolverContext::recv_vect_from(uint8_t sender)
    {
        core.recv_vect.insert(sender);
    }

    size_t SolverContext::cnt_vect_recv() const
    {
        return core.recv_vect.size();
    }

    bool SolverContext::is_matr_recv_from(uint8_t sender) const
    {
        return core.recv_matr.find(sender) != core.recv_matr.cend();
    }

    void SolverContext::recv_matr_from(uint8_t sender)
    {
        core.recv_matr.insert(sender);
    }

    size_t SolverContext::cnt_matr_recv() const
    {
        return core.recv_matr.size();
    }

    bool SolverContext::is_hash_recv_from(const PublicKey& sender) const
    {
        return (std::find(core.recv_hash.cbegin(), core.recv_hash.cend(), sender) != core.recv_hash.cend());
    }

    void SolverContext::recv_hash_from(const PublicKey& sender)
    {
        core.recv_hash.push_back(sender);
    }

    size_t SolverContext::cnt_hash_recv() const
    {
        return core.recv_hash.size();
    }

} // slv2
