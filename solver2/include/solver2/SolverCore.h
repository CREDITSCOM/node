#pragma once

#include "Solver/CallsQueueScheduler.h"
#include "INodeState.h"
#include "Consensus.h"

#include <memory>
#include <map>
#include <vector>
#include <set>
#include <algorithm>

// forward declarations

#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
#endif

class Node;
namespace Credits
{
    class Solver;
    class Generals;
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

        explicit SolverCore(Node * pNode);

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
        void gotTransactionList(csdb::Pool& pool);
        void gotVector(const Credits::HashVector& vect);
        void gotMatrix(const Credits::HashMatrix& matr);
        void gotBlock(csdb::Pool& pool, const PublicKey& sender);
        void gotBlockRequest(const csdb::PoolHash& pool_hash, const PublicKey& sender);
        void gotBlockReply(csdb::Pool& pool);
        void gotHash(const Hash& hash, const PublicKey& sender);
        void addConfirmation(uint8_t own_conf_number);
        void beforeNextRound();
        void nextRound();
        // required by api
        void send_wallet_transaction(const csdb::Transaction& tr);
        // empty in Solver
        void gotBadBlockHandler(const csdb::Pool& /*pool*/, const PublicKey& /*sender*/)
        {}

    private:
        // options

        bool opt_timeouts_enabled;
        bool opt_repeat_state_enabled;
        bool opt_spammer_on;
        bool opt_is_proxy_v1;

        // inner data

        // to use private data while serve for states as SolverCore context:
        friend class SolverContext;
        std::unique_ptr<SolverContext> pcontext;

        CallsQueueScheduler scheduler;
        CallsQueueScheduler::CallTag tag_state_expired;

        bool req_stop;

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

        std::map<StatePtr, Transitions> transitions;
        StatePtr pstate;

        void InitTransitions();
        void setState(const StatePtr& pState);

        void handleTransitions(Event evt);
        bool stateCompleted(Result result);

        // consensus data
        
        Counter cur_round;

        KeyType public_key;
        KeyType private_key;

        std::unique_ptr<Credits::HashVector> pown_hvec;

        // senders of vectors received this round
        std::set<uint8_t> recv_vect;
        // senders of matrices received this round
        std::set<uint8_t> recv_matr;
        // senders of hashes received this round
        std::vector<PublicKey> recv_hash;

        // sequence number of the last transaction received
        uint64_t last_trans_list_recv;

        // pool for collecting "own" transactions
        csdb::Pool m_pool {};

        std::mutex trans_mtx;
        std::vector<csdb::Transaction> transactions;

        // consensus private members (copied from solver.v1): по мере переноса функционала из солвера-1 могут измениться или удалиться
        void sendCurrentBlock(); // m_pool-related
        void addTimestampToPool(csdb::Pool& pool);
        void flushTransactions();
        bool verify_signature(const csdb::Transaction& tr);

        // previous solver version instance
        std::unique_ptr<Credits::Solver> pslv_v1;

        Node * pnode;
        Credits::Generals * pgen;
    };

} // slv2
