#pragma once

#include "Solver/CallsQueueScheduler.h"
#include "INodeState.h"
#include "Consensus.h"

#include <memory>
#include <map>

// forward declarations

#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
#include <vector> // to define byte_array
namespace csdb
{
    class PoolHash;

    namespace internal
    {
        using byte_array = std::vector<std::uint8_t>;
    }
}

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

    // "Интерфейсный" класс для обращений из состояний к ядру солвера,
    // ограничивает подмножество вызовов солвера, которые допускаются из классов состояний
    class SolverContext
    {
    public:
        SolverContext() = delete;

        explicit SolverContext(SolverCore& core)
            : core(core)
        {}

        void becomeNormal();

        void becomeTrusted();

        void becomeWriter();

        void startNewRound();

        inline Node& node();

        inline Credits::Generals& generals();

        // candidates for refactor:
        
        void makeAndSendBlock();
        void makeAndSendBadBlock();

    private:
        SolverCore& core;
    };

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

        // below are the "required" methods to be implemented by Solver-compatibility issue:
        
        const Credits::HashVector& getMyVector() const;
        const Credits::HashMatrix& getMyMatrix() const;
        void set_keys(const csdb::internal::byte_array& pub, const csdb::internal::byte_array& priv);
        void addInitialBalance();
        void setBigBangStatus(bool status);
        void gotTransaction(const csdb::Transaction& trans);
        void gotTransactionList(const csdb::Pool& pool);
        void gotVector(const Credits::HashVector& vect);
        void gotMatrix(const Credits::HashMatrix& matr);
        void gotBlock(const csdb::Pool& pool, const PublicKey& sender);
        void gotBlockRequest(const csdb::PoolHash& pool_hash, const PublicKey& sender);
        void gotBlockReply(const csdb::Pool& pool);
        void gotHash(const Hash& hash, const PublicKey& sender);
        void addConfirmation(uint8_t conf_number);
        void beforeNextRound();
        void nextRound();
        // required by api
        void send_wallet_transaction(const csdb::Transaction& trans);
        // empty in Solver
        void gotBadBlockHandler(const csdb::Pool& /*pool*/, const PublicKey& /*sender*/)
        {}

    private:
        // options

        bool opt_timeouts_enabled;
        bool opt_repeat_state_enabled;

        // inner data

        // to allow act as SolverCore
        friend class SolverContext;
        SolverContext context;

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
            SetWriter
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

        csdb::internal::byte_array public_key;
        csdb::internal::byte_array private_key;
        csdb::Pool m_pool; // copied from solver.v1
        csdb::Pool v_pool; // copied from solver.v1
        csdb::Pool b_pool; // copied from solver.v1

        // consensus private members (copied from solver.v1)
        void prepareBlockAndSend(); // m_pool
        void prepareBadBlockAndSend(); // b_pool
        void addTimestampToPool(csdb::Pool& pool);

        // previous solver version instance
        std::unique_ptr<Credits::Solver> pslv_v1;

        Node * pnode;
        Credits::Generals * pgen;
    };

    Node& SolverContext::node()
    {
        return *core.pnode;
    }

    Credits::Generals& SolverContext::generals()
    {
        return *core.pgen;
    }

} // slv2
