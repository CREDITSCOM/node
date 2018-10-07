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

    /**
     * @class   SolverContext
     *
     * @brief   A solver context.
     *          
     *          "Интерфейсный" класс для обращений из классов состояний к ядру солвера, определяет
     *          подмножество вызовов солвера, которые доступны из классов состояний, д. б.
     *          достаточным, но не избыточным одновременно.
     *
     * @author  aae
     * @date    03.10.2018
     */

    class SolverContext
    {
    public:
        SolverContext() = delete;

        explicit SolverContext(SolverCore& core)
            : core(core)
        {}

        // Switch state methods:

        /**
         * @fn  inline void SolverContext::become_normal();
         *
         * @brief   Request to become normal node.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline void become_normal();

        /**
         * @fn  inline void SolverContext::become_trusted();
         *
         * @brief   Request to become trusted node.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline void become_trusted();

        /**
         * @fn  inline void SolverContext::become_writer();
         *
         * @brief   Request to become writer node.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */

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

        /**
         * @fn  inline void SolverContext::vectors_completed();
         *
         * @brief   Inform that receive enough vectors.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */
        
        inline void vectors_completed();

        /**
         * @fn  inline void SolverContext::matrices_completed();
         *
         * @brief   Inform that receive enough matrices.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline void matrices_completed();

        /**
         * @fn  void SolverContext::spawn_next_round();
         *
         * @brief   Spawn next round.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */

        void spawn_next_round();

        // Fast access methods, may be removed at the end

        /**
         * @fn  inline Node& SolverContext::node() const;
         *
         * @brief   Gets the node instance.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  A reference to a Node.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline Node& node() const;

        /**
         * @fn  inline Credits::Generals& SolverContext::generals() const;
         *
         * @brief   Gets the generals instance.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  A reference to the Credits::Generals.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline Credits::Generals& generals() const;

        /**
         * @fn  inline CallsQueueScheduler& SolverContext::scheduler() const;
         *
         * @brief   Gets the scheduler instance.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  A reference to a CallsQueueScheduler.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline CallsQueueScheduler& scheduler() const;

        // Access to common state properties. 
        
        /// <summary>   Public key. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   A reference to a const KeyType public key. </returns>

        inline const KeyType& public_key() const;

        /**
         * @fn  inline const KeyType& SolverContext::private_key() const;
         *
         * @brief   Private key.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  A reference to a const KeyType private key.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline const KeyType& private_key() const;

        /**
         * @fn  inline const Credits::HashVector& SolverContext::hash_vector() const;
         *
         * @brief   Current hash vector.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  A reference to a const Credits::HashVector.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline const Credits::HashVector& hash_vector() const;

        /**
         * @fn  inline uint32_t SolverContext::round() const;
         *
         * @brief   Gets the current round number.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  An int32_t.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline uint32_t round() const;

        /**
         * @fn  uint8_t SolverContext::own_conf_number() const;
         *
         * @brief   Gets the own number among confidant (trusted) nodes.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  An uint8_t.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        uint8_t own_conf_number() const;

        /**
         * @fn  size_t SolverContext::cnt_trusted() const;
         *
         * @brief   Gets count of trusted nodes in current round.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  The total number of trusted.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        size_t cnt_trusted() const;

        /**
         * @fn  inline bool SolverContext::is_spammer() const;
         *
         * @brief   Query if this node is in spammer mode.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  True if spammer, false if not.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline bool is_spammer() const;

        // Common operations, candidates for refactoring:

        /**
         * @fn  inline void SolverContext::store_and_send_block();
         *
         * @brief   Makes a block from inner pool of collected and validated transactions and send it
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline void store_and_send_block();

        /**
         * @fn  inline void SolverContext::resend_last_block();
         *
         * @brief   Resend last block
         *
         * @author  aae
         * @date    02.10.2018
         */

        inline void repeat_last_block();

        /**
         * @fn  inline void SolverContext::add(const csdb::Transaction& tr);
         *
         * @brief   Adds transaction to inner list
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @param   tr  The tr to add.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline void add(const csdb::Transaction& tr);

        /**
         * @fn  inline void SolverContext::flush_transactions();
         *
         * @brief   Sends the transactions in inner list
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return count of flushed transactions
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline size_t flush_transactions();

        /**
         * @fn  inline bool SolverContext::verify(const csdb::Transaction& tr) const;
         *
         * @brief   Verifies the given transaction
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @param   tr  The tr.
         *
         * @return  True if it succeeds, false if it fails.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline bool verify(const csdb::Transaction& tr) const;

        /**
         * @fn  inline bool SolverContext::is_vect_recv_from(uint8_t sender) const;
         *
         * @brief   Query if is vector received from passed sender
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @param   sender  The sender.
         *
         * @return  True if vect receive from, false if not.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline bool is_vect_recv_from(uint8_t sender) const;

        /**
         * @fn  inline void SolverContext::recv_vect_from(uint8_t sender);
         *
         * @brief   Inform core to remember that vector from passed sender is received
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @param   sender  The sender.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline void recv_vect_from(uint8_t sender);

        /**
         * @fn  inline size_t SolverContext::cnt_vect_recv() const;
         *
         * @brief   Count of vectors received
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  The total number of vect receive.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline size_t cnt_vect_recv() const;

        /**
         * @fn  inline bool SolverContext::is_matr_recv_from(uint8_t sender) const;
         *
         * @brief   Query if is matrix received from passed sender
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @param   sender  The sender.
         *
         * @return  True if matr receive from, false if not.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline bool is_matr_recv_from(uint8_t sender) const;

        /**
         * @fn  inline void SolverContext::recv_matr_from(uint8_t sender);
         *
         * @brief   Inform core to remember that matrix from passed sender is received
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @param   sender  The sender.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline void recv_matr_from(uint8_t sender);

        /**
         * @fn  inline size_t SolverContext::cnt_matr_recv() const;
         *
         * @brief   Count of matrices received
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  The total number of matr receive.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline size_t cnt_matr_recv() const;

        /**
         * @fn  inline bool SolverContext::is_hash_recv_from(const PublicKey& sender) const;
         *
         * @brief   Query if is hash received from passed sender
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @param   sender  The sender.
         *
         * @return  True if hash receive from, false if not.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline bool is_hash_recv_from(const PublicKey& sender) const;

        /**
         * @fn  inline void SolverContext::recv_hash_from(const PublicKey& sender);
         *
         * @brief   Inform core to remember that hash from passed sender is received
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @param   sender  The sender.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline void recv_hash_from(const PublicKey& sender);

        /**
         * @fn  inline size_t SolverContext::cnt_hash_recv() const;
         *
         * @brief   Count of hashes received
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  The total number of hash receive.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        inline size_t cnt_hash_recv() const;

        inline csdb::Address address_spammer() const;
        inline csdb::Address address_genesis() const;
        inline csdb::Address address_start() const;

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

    uint32_t SolverContext::round() const
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

    void SolverContext::store_and_send_block()
    {
        core.prepareBlock(core.pool);
        core.sendBlock(core.pool);
        core.storeBlock(core.pool);
    }

    void SolverContext::repeat_last_block()
    {
        core.repeatLastBlock();
    }

    void SolverContext::add(const csdb::Transaction& tr)
    {
        core.send_wallet_transaction(tr);
    }

    size_t SolverContext::flush_transactions()
    {
        return core.flushTransactions();
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

    csdb::Address SolverContext::address_spammer() const
    {
        return core.addr_spam.value_or(csdb::Address {});
    }

    csdb::Address SolverContext::address_genesis() const
    {
        return core.addr_genesis;
    }

    inline csdb::Address SolverContext::address_start() const
    {
        return core.addr_start;
    }

} // slv2
