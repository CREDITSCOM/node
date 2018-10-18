#pragma once

#include "INodeState.h"
#include "SolverCore.h"

#include <csdb/pool.h>
#include <lib/system/keys.hpp>

class CallsQueueScheduler;
class Node;
class BlockChain;

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
     * @enum    Role
     *
     * @brief   Values that represent roles, repeats analog defined in node.hpp
     */

    enum class Role
    {
        Normal,
        Trusted,
        Collect,
        Write
    };

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

        /**
         * @fn  void SolverContext::request_role(Role role)
         *
         * @brief   Request core to activate one of predefined role (@see Role). Obviously it is achieved
         *          by switching current state. Request may be "ignored" depending on current state and
         *          content of transition table.
         *
         * @author  Alexander Avramenko
         * @date    15.10.2018
         *
         * @param   role    The role requested.
         */

        void request_role(Role role)
        {
            switch(role) {
            case Role::Normal:
                core.handleTransitions(SolverCore::Event::SetNormal);
                break;
            case Role::Trusted:
                core.handleTransitions(SolverCore::Event::SetTrusted);
                break;
            case Role::Collect:
                core.handleTransitions(SolverCore::Event::SetCollector);
                break;
            case Role::Write:
                core.handleTransitions(SolverCore::Event::SetWriter);
                break;
            }
        }

        /**
         * @fn  NodeLevel SolverContext::level() const;
         *
         * @brief   Gets the current node role as set in last round table
         *
         * @author  Alexander Avramenko
         * @date    15.10.2018
         *
         * @return  A node role set in last round table.
         */

        Role role() const;

        /**
         * @fn  void SolverContext::vectors_completed();
         *
         * @brief   Inform core that enough vectors are received.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */

        void vectors_completed()
        {
            core.handleTransitions(SolverCore::Event::Vectors);
        }

        /**
         * @fn  void SolverContext::matrices_completed();
         *
         * @brief   Inform core that enough matrices are received.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */

        void matrices_completed()
        {
            core.handleTransitions(SolverCore::Event::Matrices);
        }

        /**
         * @fn  void SolverContext::spawn_next_round();
         *
         * @brief   Spawn request to next round.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */

        void spawn_next_round();

        // Fast access methods, may be removed at the end

		/**
		 * @fn	BlockChain& SolverContext::blockchain() const;
		 *
		 * @brief	Gets the blockchain
		 *
		 * @author	User
		 * @date	09.10.2018
		 *
		 * @return	A reference to a BlockChain.
		 */

		BlockChain& blockchain() const;

        /**
         * @fn  cs::Generals& SolverContext::generals() const;
         *
         * @brief   Gets the generals instance.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  A reference to the cs::Generals.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        cs::Generals& generals() const
        {
            return *core.pgen;
        }

        /**
         * @fn  CallsQueueScheduler& SolverContext::scheduler() const;
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

        CallsQueueScheduler& scheduler() const
        {
            return core.scheduler;
        }

        // Access to common state properties. 

        /**
         * @fn  const KeyType& SolverContext::public_key() const
         *
         * @brief   Public key.
         *
         * @author  Alexander Avramenko
         * @date    10.10.2018
         *
         * @return  A reference to a const KeyType public key.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        const KeyType& public_key() const
        {
            return core.public_key;
        }

        /**
         * @fn  const KeyType& SolverContext::private_key() const;
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

        const KeyType& private_key() const
        {
            return core.private_key;
        }

        /**
         * @fn  const cs::HashVector& SolverContext::hash_vector() const;
         *
         * @brief   Current hash vector.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  A reference to a const cs::HashVector.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        const cs::HashVector& hash_vector() const
        {
            return core.getMyVector();
        }

        /**
         * @fn  const cs::HashMatrix& SolverContext::hash_matrix() const;
         *
         * @brief   Hash matrix
         *          
         *
         * @author  Alexander Avramenko
         * @date    15.10.2018
         *
         * @return  A reference to a const cs::HashMatrix.
         */

        const cs::HashMatrix& hash_matrix() const
        {
            return core.getMyMatrix();
        }

        /**
         * @fn  uint32_t SolverContext::round() const;
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

        uint32_t round() const
        {
            return core.cur_round;
        }

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

        size_t own_conf_number() const;

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
         * @fn  size_t SolverContext::cnt_trusted_desired() const;
         *
         * @brief   Gets preferred count of trusted nodes for any round.
         *
         * @author  aae
         * @date    03.10.2018
         *
         * @return  The desired number of trusted nodes for any round.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        size_t cnt_trusted_desired() const
        {
            return core.cnt_trusted_desired;
        }

        /**
         * @fn  const std::vector<PublicKey>& SolverContext::trusted() const;
         *
         * @brief   Gets the trusted
         *
         * @author  Alexander Avramenko
         * @date    15.10.2018
         *
         * @return  A reference to a const std::vector&lt;PublicKey&gt;
         *          
         */

        const std::vector<cs::PublicKey>& trusted() const;

        /**
         * @fn  bool SolverContext::is_spammer() const;
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

        bool is_spammer() const
        {
            return core.opt_spammer_on;
        }

        /**
         * @fn  void SolverContext::store_received_block(csdb::Pool & block);
         *
         * @brief   Stores received block
         *
         * @author  Alexander Avramenko
         * @date    10.10.2018
         *
         * @param [in,out]  block   The block.
         */

        void store_received_block(csdb::Pool & block)
        {
            core.storeReceivedBlock(block);
        }

        // Common operations, candidates for refactoring:

        /**
         * @fn  void SolverContext::create_and_send_new_block();
         *
         * @brief   Makes a block from inner pool of collected and validated transactions and send it
         *
         * @author  aae
         * @date    03.10.2018
         *
         * ### remarks  Aae, 30.09.2018.
         */

        void create_and_send_new_block()
        {
            core.createAndSendNewBlock();
        }

        /**
         * @fn  void SolverContext::create_and_send_new_block_from(csdb::Pool& p)
         *
         * @brief   Creates and send new block from passed pool of transactions
         *
         * @author  Alexander Avramenko
         * @date    11.10.2018
         *
         * @param [in,out]  p   A csdb::Pool to process.
         */

        void create_and_send_new_block_from(csdb::Pool& p)
        {
            core.createAndSendNewBlockFrom(p);
        }

        /**
         * @fn  void SolverContext::repeat_last_block()
         *
         * @brief   Resend last block
         *
         * @author  aae
         * @date    02.10.2018
         */

        void repeat_last_block()
        {
            core.repeatLastBlock();
        }

        /**
         * @fn  void SolverContext::request_round_table() const;
         *
         * @brief   Request round table
         *          
         *
         * @author  Alexander Avramenko
         * @date    15.10.2018
         */

        void request_round_table() const;

        /**
         * @fn  void SolverContext::add(const csdb::Transaction& tr);
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

        void add(const csdb::Transaction& tr)
        {
            core.send_wallet_transaction(tr);
        }

        /**
         * @fn  void SolverContext::flush_transactions();
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

        size_t flush_transactions()
        {
            return core.flushTransactions();
        }

        /**
         * @fn  bool SolverContext::is_vect_recv_from(uint8_t sender) const;
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

        bool is_vect_recv_from(uint8_t sender) const
        {
            return core.recv_vect.find(sender) != core.recv_vect.cend();
        }

        /**
         * @fn  void SolverContext::recv_vect_from(uint8_t sender);
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

        void recv_vect_from(uint8_t sender)
        {
            core.recv_vect.insert(sender);
        }

        /**
         * @fn  size_t SolverContext::cnt_vect_recv() const;
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

        size_t cnt_vect_recv() const
        {
            return core.recv_vect.size();
        }

        /**
         * @fn  bool SolverContext::is_matr_recv_from(uint8_t sender) const;
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

        bool is_matr_recv_from(uint8_t sender) const
        {
            return core.recv_matr.find(sender) != core.recv_matr.cend();
        }

        /**
         * @fn  void SolverContext::recv_matr_from(uint8_t sender);
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

        void recv_matr_from(uint8_t sender)
        {
            core.recv_matr.insert(sender);
        }

        /**
         * @fn  size_t SolverContext::cnt_matr_recv() const;
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

        size_t cnt_matr_recv() const
        {
            return core.recv_matr.size();
        }

        /**
         * @fn  bool SolverContext::is_hash_recv_from(const cs::PublicKey& sender) const;
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

        bool is_hash_recv_from(const cs::PublicKey& sender) const
        {
            return (std::find(core.recv_hash.cbegin(), core.recv_hash.cend(), sender) != core.recv_hash.cend());
        }

        /**
         * @fn  void SolverContext::recv_hash_from(const cs::PublicKey& sender);
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

        void recv_hash_from(const cs::PublicKey& sender)
        {
            core.recv_hash.push_back(sender);
        }

        /**
         * @fn  size_t SolverContext::cnt_hash_recv() const;
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

        size_t cnt_hash_recv() const
        {
            return core.recv_hash.size();
        }

        /**
         * @fn  csdb::Address SolverContext::address_spammer() const
         *
         * @brief   Address spammer
         *
         * @author  Alexander Avramenko
         * @date    10.10.2018
         *
         * @return  The csdb::Address.
         */

        csdb::Address address_spammer() const
        {
            return core.addr_spam.value_or(csdb::Address {});
        }

        /**
         * @fn  csdb::Address SolverContext::address_genesis() const
         *
         * @brief   Address genesis
         *
         * @author  Alexander Avramenko
         * @date    10.10.2018
         *
         * @return  The csdb::Address.
         */

        csdb::Address address_genesis() const
        {
            return core.addr_genesis;
        }

        /**
         * @fn  csdb::Address SolverContext::address_start() const
         *
         * @brief   Address start
         *
         * @author  Alexander Avramenko
         * @date    10.10.2018
         *
         * @return  The csdb::Address.
         */

        csdb::Address address_start() const
        {
            return core.addr_start;
        }


		/**
		 * @fn	csdb::Address SolverContext::optimize(const csdb::Address& address) const;
		 *
		 * @brief	Optimizes the given address. Tries to get wallet id from blockchain, otherwise return dicrect address
		 *
		 * @author	User
		 * @date	09.10.2018
		 *
		 * @param	address	The address to optimize.
		 *
		 * @return	The csdb::Address optimized with id if possible
		 */

		csdb::Address optimize(const csdb::Address& address) const;

        /**
         * @fn  void SolverContext::send_hash(const Hash& hash, const PublicKey& target);
         *
         * @brief   Sends a hash to a target
         *
         * @author  Alexander Avramenko
         * @date    15.10.2018
         *
         * @param   hash    The hash.
         * @param   target  Target for the.
         */

        void send_hash(const cs::Hash& hash, const cs::PublicKey& target);

        /**
         * @fn  void SolverContext::send_own_vector();
         *
         * @brief   Sends the own vector
         *
         * @author  Alexander Avramenko
         * @date    15.10.2018
         */

        void send_own_vector();

        /**
         * @fn  void SolverContext::send_own_matrix();
         *
         * @brief   Sends the own matrix
         *
         * @author  Alexander Avramenko
         * @date    15.10.2018
         */

        void send_own_matrix();

        /**
         * @fn  void SolverContext::send_transaction_list(csdb::Pool& pool);
         *
         * @brief   Sends a transaction list
         *
         * @author  Alexander Avramenko
         * @date    15.10.2018
         *
         * @param [in,out]  pool    The pool.
         */

        void send_transaction_list(csdb::Pool& pool);

        // methods to operate with vectors cache

        void cache_vector(uint8_t sender, const cs::HashVector& vect)
        {
            core.cache_vector(sender, vect);
        }

        // looks for vector in matrices already received, returns nullptr if vector not found:
        const cs::HashVector* lookup_vector(uint8_t sender) const
        {
            return core.lookup_vector(sender);
        }

        void clear_vectors_cache()
        {
            core.clear_vectors_cache();
        }

    private:
        SolverCore& core;
    };

} // slv2
