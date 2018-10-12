#pragma once

#include "CallsQueueScheduler.h"
#include "INodeState.h"

#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
#endif

#include <memory>
#include <map>
#include <vector>
#include <set>
#include <algorithm>
#include <optional>

// forward declarations
class Node;
namespace Credits
{
    class WalletsState;
    class Solver;
    class Generals;
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
        
        const Credits::HashVector& getMyVector() const;
        const Credits::HashMatrix& getMyMatrix() const;
        void set_keys(const KeyType& pub, const KeyType& priv);
        void addInitialBalance();
        void setBigBangStatus(bool status);
        void gotTransaction(const csdb::Transaction& trans);
        void gotTransactionList(csdb::Pool& p);
        void gotVector(const Credits::HashVector& vect);
        void gotMatrix(const Credits::HashMatrix& matr);
        void gotBlock(csdb::Pool& p, const PublicKey& sender);
        void gotBlockRequest(const csdb::PoolHash& p_hash, const PublicKey& sender);
        void gotBlockReply(csdb::Pool& p);
        void gotHash(const Hash& hash, const PublicKey& sender);
        void gotIncorrectBlock(csdb::Pool&& p, const PublicKey& sender);
        // store outrunning syncro blocks
        void gotFreeSyncroBlock(csdb::Pool&& p);
        // retrieve outrunning syncro blocks and store them
        void rndStorageProcessing();
        void tmpStorageProcessing();
        void addConfirmation(uint8_t own_conf_number);
        void beforeNextRound();
        void nextRound();
        // required by api
        void send_wallet_transaction(const csdb::Transaction& tr);
        // empty in Solver
        void gotBadBlockHandler(const csdb::Pool& /*p*/, const PublicKey& /*sender*/)
        {}

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
        std::unique_ptr<Credits::HashVector> pown_hvec;
        std::unique_ptr<Credits::Fee> pfee;
        // senders of vectors received this round
        std::set<uint8_t> recv_vect;
        // senders of matrices received this round
        std::set<uint8_t> recv_matr;
        // senders of hashes received this round
        std::vector<PublicKey> recv_hash;
        // sequence number of the last transaction received
        uint64_t last_trans_list_recv;
        // pool for storing transactions list, serve as source for new block
        // must be managed by SolverCore because consumed by different states (Trusted*, Write...)
        csdb::Pool pool {};
        std::mutex trans_mtx;
        csdb::Pool transactions;
        // to store outrunning blocks until the time comes
        // stores pairs of <block, sender> sorted by sequence number
        std::map<csdb::Pool::sequence_t, std::pair<csdb::Pool,PublicKey>> outrunning_blocks;
        // to store unrequested syncro blocks
        std::map <size_t, csdb::Pool> rnd_storage;
        // store BB status to reproduce solver-1 logic
        bool is_bigbang;

        // previous solver version instance

        std::unique_ptr<Credits::Solver> pslv_v1;
        Node * pnode;
        std::unique_ptr<Credits::WalletsState> pws_inst;
        Credits::WalletsState * pws;
        std::unique_ptr<Credits::Generals> pgen_inst;
        Credits::Generals * pgen;

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
            createAndSendNewBlockFrom(pool);
        }
        void storeReceivedBlock(csdb::Pool& p);
        // returns count of transactions flushed
        size_t flushTransactions();
        csdb::Pool removeTransactionsWithBadSignatures(const csdb::Pool& p);
    };

} // slv2
