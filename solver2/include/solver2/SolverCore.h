#pragma once

#include "CallsQueueScheduler.h"
#include "INodeState.h"

#include "SolverCompat.h" // temporary, while cs::HashVector defined there
#include <csdb/pool.h>
#include <lib/system/keys.hpp>
#include <solver/Fee.h>
#include <solver/solver.hpp>
#include <solver/WalletsState.h>
#include <csnode/nodecore.h>

#include <memory>
#include <map>
#include <vector>
#include <set>
#include <algorithm>
#include <optional>
#include <array>

// forward declarations
class Node;
namespace Credits
{
    class WalletsState;
    class Solver;
    class Generals;
    class Fee;
}

namespace cs {
    class Fee;
}

//TODO: discuss possibility to switch states after timeout expired, timeouts can be individual but controlled by SolverCore

namespace slv2
{

    class SolverCore;

    using KeyType = csdb::internal::byte_array;

    class SolverCore
    {
    public:
        using Counter = uint32_t;

        SolverCore();
        explicit SolverCore(Node * pNode, csdb::Address GenesisAddress, csdb::Address StartAddress, std::optional<csdb::Address> SpammerAddres = {});

        ~SolverCore();

        void start();

        void finish();

        bool is_finished() const
        {
            return req_stop;
        }

        // Solver "public" interface,
        // below are the "required" methods to be implemented by Solver-compatibility issue:

        const cs::HashVector& getMyVector() const;
        const cs::HashMatrix& getMyMatrix() const;
        void set_keys(const KeyType& pub, const KeyType& priv);
        void addInitialBalance();
        void setBigBangStatus(bool status);
        void gotTransaction(const csdb::Transaction& trans);
        void gotTransactionList(csdb::Pool& p);
        void gotVector(const cs::HashVector& vect);
        void gotMatrix(cs::HashMatrix&& matr);
        void gotBlock(csdb::Pool&& p, const cs::PublicKey& sender);
        void gotBlockRequest(const csdb::PoolHash& p_hash, const cs::PublicKey& sender);
        void gotBlockReply(csdb::Pool&& p);
        void gotHash(const cs::Hash& hash, const cs::PublicKey& sender);
        void gotIncorrectBlock(csdb::Pool&& p, const cs::PublicKey& sender);
        // store outrunning syncro blocks
        void gotFreeSyncroBlock(csdb::Pool&& p);
        // retrieve outrunning syncro blocks and store them
        void rndStorageProcessing();
        void tmpStorageProcessing();
        void addConfirmation(uint8_t own_conf_number);
        void beforeNextRound();
        void nextRound();

        /// <summary>   Adds a transaction passed to send pool </summary>
        ///
        /// <remarks>   Aae, 14.10.2018. </remarks>
        ///
        /// <param name="tr">   The transaction </param>

        void send_wallet_transaction(const csdb::Transaction& tr);

        /// <summary>
        ///     Returns the nearest absent in cache block number starting after passed one. If all
        ///     required blocks are in cache returns 0.
        /// </summary>
        ///
        /// <remarks>   Aae, 14.10.2018. </remarks>
        ///
        /// <param name="starting_after">   The block after which to start search of absent one. </param>
        ///
        /// <returns>   The next missing block sequence number or 0 if all blocks are present. </returns>

        csdb::Pool::sequence_t getNextMissingBlock(const uint32_t starting_after) const;

        /// <summary>
        ///     Gets count of cached block in passed range.
        /// </summary>
        ///
        /// <remarks>   Aae, 14.10.2018. </remarks>
        ///
        /// <param name="starting_after">   The block after which to start count cached ones. </param>
        /// <param name="end">              The block up to which count. </param>
        ///
        /// <returns>
        ///     Count of cached blocks in passed range.
        /// </returns>

        csdb::Pool::sequence_t getCountCahchedBlock(csdb::Pool::sequence_t starting_after, csdb::Pool::sequence_t end ) const;

        // empty in Solver
        void gotBadBlockHandler(const csdb::Pool& /*p*/, const cs::PublicKey& /*sender*/) const
        {}

        void setKeysPair(const cs::PublicKey& publicKey, const cs::PrivateKey& privateKey);
        void runSpammer();
        void gotRound();
        bool getIPoolClosed();
        void gotHash(std::string&&, const cs::PublicKey&);
        const cs::PrivateKey& getPrivateKey() const;
        const cs::PublicKey& getPublicKey() const;
        cs::PublicKey getWriterPublicKey() const;
        bool getBigBangStatus();
        bool isPoolClosed() const;
        NodeLevel nodeLevel() const;
        const cs::PublicKey& nodePublicKey() const;
        void countFeesInPool(csdb::Pool* pool);

    private:

        // to use private data while serve for states as SolverCore context:
        friend class SolverContext;

        enum class Event
        {
            Start,
            BigBang,
            RoundTable,
            Transactions,
            Block,
            Hashes,
            Vectors,
            Matrices,
            SyncData,
            Expired,
            SetNormal,
            SetTrusted,
            SetWriter,
            SetCollector
        };

        using StatePtr = std::shared_ptr<INodeState>;
        using Transitions = std::map<Event, StatePtr>;

        // options

        /** @brief   True to enable, false to disable the option to track timeout of current state */
        bool opt_timeouts_enabled;

        /** @brief   True to enable, false to disable the option repeat the same state */
        bool opt_repeat_state_enabled;

        /** @brief   True to enable, false to disable the option of spammer activation */
        bool opt_spammer_on;

        /** @brief   True if proxy to solver-1 mode is on */
        bool opt_is_proxy_v1;

        /** @brief   True if option is permanent node roles is on */
        bool opt_is_permanent_roles;

        // inner data

        std::unique_ptr<SolverContext> pcontext;
        CallsQueueScheduler scheduler;
        CallsQueueScheduler::CallTag tag_state_expired;
        bool req_stop;
        std::map<StatePtr, Transitions> transitions;
        StatePtr pstate;
        size_t cnt_trusted_desired;

        // consensus data
        
        csdb::Address addr_genesis;
        csdb::Address addr_start;
        std::optional<csdb::Address> addr_spam;
        Counter cur_round;
        KeyType public_key;
        KeyType private_key;
        std::unique_ptr<cs::HashVector> pown_hvec;
        std::unique_ptr<cs::Fee> pfee;
        // senders of vectors received this round
        std::set<uint8_t> recv_vect;
        // senders of matrices received this round
        std::set<uint8_t> recv_matr;
        // senders of hashes received this round
        std::vector<cs::PublicKey> recv_hash;
        // sequence number of the last transaction received
        uint64_t last_trans_list_recv;
        // pool for storing transactions list, serve as source for new block
        // must be managed by SolverCore because is consumed by different states (Trusted*, Write...)
        csdb::Pool block_pool {};
        std::mutex trans_mtx;
        // pool for storing individual transactions from wallets and candidates for transaction list,
        // serve as source for flushTransactions()
        csdb::Pool trans_pool {};
        // to store outrunning blocks until the time to insert them comes
        // stores pairs of <block, sender> sorted by sequence number
        std::map<csdb::Pool::sequence_t, std::pair<csdb::Pool,cs::PublicKey>> outrunning_blocks;
        // store BB status to reproduce solver-1 logic
        bool is_bigbang;

        // vectors reserve storage (extracted from matrices if any)
        using VectorVariant = std::pair<cs::HashVector, size_t>; // {vector -> count}
        std::map<uint8_t, std::vector<VectorVariant>> vector_cache; // {sender -> vector variants}

        //const static uint8_t NoSender = 0xFF;
        //std::array<cs::HashMatrix, cs::HashMatrixMaxGen> matrices;

        // previous solver version instance

        std::unique_ptr<cs::Solver> pslv_v1;
        Node * pnode;
        std::unique_ptr<cs::WalletsState> pws_inst;
        cs::WalletsState * pws;
        std::unique_ptr<cs::Generals> pgen_inst;
        cs::Generals * pgen;

        void InitTransitions();
        void InitPermanentTransitions();
        void setState(const StatePtr& pState);

        void handleTransitions(Event evt);
        bool stateCompleted(Result result);
        // scans cached before blocks and retrieve them for processing if good sequence number
        void test_outrunning_blocks();
        // sends current block if actual otherwise loads block from storage and sends it
        void repeatLastBlock();

        // consensus private members (copied from solver.v1): по мере переноса функционала из солвера-1 могут измениться или удалиться

        void createAndSendNewBlockFrom(csdb::Pool& p);
        void createAndSendNewBlock()
        {
            createAndSendNewBlockFrom(block_pool);
        }
        void storeReceivedBlock(csdb::Pool& p);

        /// <summary>   Flushes transactions stored in inner trans_pool and clears trans_pool for future use. 
        ///             TODO: maybe clear every transaction individually after confirmation it is in one of future blocks</summary>
        ///
        /// <remarks>   Aae, 14.10.2018. </remarks>
        ///
        /// <returns>   Count of transactions flushed. </returns>

        size_t flushTransactions();
        csdb::Pool removeTransactionsWithBadSignatures(const csdb::Pool& p);

        // methods to operate with vectors cache
        void cache_vector(uint8_t sender, const cs::HashVector& vect);
        // looks for vector in matrices already received, returns nullptr if vector not found:
        const cs::HashVector* lookup_vector(uint8_t sender) const;
        // 
        void clear_vectors_cache()
        {
            vector_cache.clear();
        }
    };

} // slv2
