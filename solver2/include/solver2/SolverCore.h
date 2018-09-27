#pragma once

#include "Solver/CallsQueueScheduler.h"
#include "INodeState.h"
#include "Consensus.h"

#include <memory>
#include <map>
#include <vector>
#include <set>

// forward declarations

#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
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

    using KeyType = csdb::internal::byte_array;

    // "Интерфейсный" класс для обращений из классов состояний к ядру солвера,
    // определяет подмножество вызовов солвера, которые доступны из классов состояний,
    // д. б. универсальными и не избыточными одновременно
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

        // fast access methods, may be removed at the end
        inline Node& node();
        inline Credits::Generals& generals();
        inline CallsQueueScheduler& scheduler();

        inline const KeyType& public_key() const;
        inline const KeyType& private_key() const;

        inline int32_t round() const;

        // candidates for refactoring:
        
        void makeAndSendBlock();
        void makeAndSendBadBlock();
        inline bool is_spammer() const;
        inline void add(const csdb::Transaction& tr);
        inline void flush_transactions();
        inline bool verify(const csdb::Transaction& tr) const;

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

        // Solver "public" interface,
        // below are the "required" methods to be implemented by Solver-compatibility issue:
        
        const Credits::HashVector& getMyVector() const;
        const Credits::HashMatrix& getMyMatrix() const;
        void set_keys(const KeyType& pub, const KeyType& priv);
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
        void send_wallet_transaction(const csdb::Transaction& tr);
        // empty in Solver
        void gotBadBlockHandler(const csdb::Pool& /*pool*/, const PublicKey& /*sender*/)
        {}

    private:
        // options

        bool opt_timeouts_enabled;
        bool opt_repeat_state_enabled;
        bool opt_spammer_on;

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

        KeyType public_key;
        KeyType private_key;

        // copied from solver.v1: по мере разнесения функционала по состояниям кол-во данных должно уменьшиться
        csdb::Pool m_pool {};
        //csdb::Pool v_pool {}; -> SelectState - накопление чужих транзакций
        //csdb::Pool b_pool {}; -> StartState? - отправка bad block
        size_t lastRoundTransactionsGot { 0 };
        std::set<PublicKey> receivedVec_ips;
        bool receivedVecFrom [100];
        uint8_t trustedCounterVector { 0 };
        std::set<PublicKey> receivedMat_ips;
        bool receivedMatFrom [100];
        uint8_t trustedCounterMatrix { 0 };
        std::vector<Hash> hashes;
        std::vector<PublicKey> ips;
        std::vector<std::string> vector_datas;
        bool m_pool_closed { true };
        bool vectorComplete { false };
        bool consensusAchieved { false };
        bool blockCandidateArrived { false };
        bool round_table_sent { false };
        bool transactionListReceived { false };
        bool vectorReceived { false };
        bool gotBlockThisRound { false };
        bool writingConfirmationGot { false };
        bool gotBigBang { false };
        bool writingConfGotFrom [100];
        uint8_t writingCongGotCurrent { 0 };
        bool allMatricesReceived { false };
        std::mutex trans_mtx;
        std::vector<csdb::Transaction> transactions;

        // consensus private members (copied from solver.v1): по мере переноса функционала из солвера-1 могут измениться или удалиться
        void prepareBlockAndSend(); // m_pool
        void prepareBadBlockAndSend(); // b_pool
        void addTimestampToPool(csdb::Pool& pool);
        void flushTransactions();
        bool verify_signature(const csdb::Transaction& tr);

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

    CallsQueueScheduler& SolverContext::scheduler()
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

    inline int32_t SolverContext::round() const
    {
        return core.cur_round;
    }

    bool SolverContext::is_spammer() const
    {
        return core.opt_spammer_on;
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

} // slv2
