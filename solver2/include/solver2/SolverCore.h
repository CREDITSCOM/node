#pragma once

#include "Solver/CallsQueueScheduler.h"
#include "INodeState.h"
#include "Consensus.h"

#if defined(SOLVER_USES_PROXY_TYPES)
//#include "ProxyTypes.h"
#else
//#include <csdb/pool.h>
//#include <Solver/Solver.hpp> // Credits::HashVector, Credits::Solver
#include <vector> // to define byte_array
#endif

#include <memory>
#include <map>

class Node;

namespace csdb
{
    class PoolHash;

    namespace internal
    {
        using byte_array = std::vector<std::uint8_t>;
    }
}

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
            : m_core(core)
        {}

        void becomeNormal();

        void becomeTrusted();

        void becomeWriter();

        void startNewRound();

    private:
        SolverCore& m_core;
    };

    class SolverCore
    {
    public:
        using Counter = uint32_t;

        SolverCore();

        SolverCore(Node * pNode);

        ~SolverCore();

        void Start();

        void Finish();

        bool isFinished() const
        {
            return m_shouldStop;
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
        // empty in Solver
        void gotBadBlockHandler(const csdb::Pool& /*pool*/, const PublicKey& /*sender*/)
        {}

    private:
        constexpr static uint32_t DefaultStateTimeout = 5000;
        CallsQueueScheduler m_scheduler;
        CallsQueueScheduler::CallTag m_stateExpiredTag;

        // options

        bool m_optTimeoutsEnabled;
        bool m_optDuplicateStateEnabled;

        // inner data

        // to allow act as SolverCore
        friend class SolverContext;
        SolverContext m_context;

        bool m_shouldStop;

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

        std::map<StatePtr, Transitions> m_transitions;
        StatePtr m_pState;

        void InitTransitions();
        void setState(const StatePtr& pState);

        void handleTransitions(Event evt);
        bool stateCompleted(Result result);

        // consensus data
        
        Counter m_round;

        // previous solver version instance
        
        std::unique_ptr<Credits::Solver> m_pSolvV1;
        Node * m_pNode;
        Credits::Generals * m_pGen;
    };
} // slv2
