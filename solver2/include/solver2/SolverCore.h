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

        inline void becomeNormal();
        inline void becomeTrusted();
        inline void becomeWriter();
        inline void startNewRound();
        inline void vectors_completed();
        inline void matrices_completed();

        // fast access methods, may be removed at the end
        inline Node& node() const;
        inline Credits::Generals& generals() const;
        inline CallsQueueScheduler& scheduler() const;

        inline const KeyType& public_key() const;
        inline const KeyType& private_key() const;

        inline int32_t round() const;
        uint8_t conf_number() const;
        size_t cnt_trusted() const;

        // candidates for refactoring:
        
        void spawn_next_round();
        inline void makeAndSendBlock();
        inline void makeAndSendBadBlock();
        inline bool is_spammer() const;
        inline void add(const csdb::Transaction& tr);
        inline void flush_transactions();
        inline bool verify(const csdb::Transaction& tr) const;
        
        inline bool is_vect_recv_from(uint8_t sender) const;
        inline void recv_vect_from(uint8_t sender);
        inline size_t cnt_vect_recv() const;
        inline bool is_matr_recv_from(uint8_t sender) const;
        inline void recv_matr_from(uint8_t sender);
        inline size_t cnt_matr_recv() const;
        inline bool is_hash_recv_from(const PublicKey& sender) const;
        inline void recv_hash_from(const PublicKey& sender);
        inline size_t cnt_hash_recv() const;

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
        bool opt_is_proxy_v1;

        // inner data

        // to allow serve states as SolverCore:
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

        std::unique_ptr<Credits::HashVector> pown_hvec;

        // senders of vectors received this round
        std::set<uint8_t> recv_vect;
        // senders of matrices received this round
        std::set<uint8_t> recv_matr;
        // senders of hashes received this round
        std::vector<PublicKey> recv_hash;

        // copied from solver.v1: по мере разнесения функционала по состояниям кол-во данных должно уменьшиться
        csdb::Pool m_pool {};
        //csdb::Pool v_pool {}; -> SelectState - накопление чужих транзакций
        //csdb::Pool b_pool {}; -> StartState? - отправка bad block
        //size_t lastRoundTransactionsGot { 0 };
        //std::set<PublicKey> receivedVec_ips;
        //std::set<PublicKey> receivedMat_ips;
        //std::vector<Hash> hashes;
        //std::vector<PublicKey> ips;
        //std::vector<std::string> vector_datas;
        //bool m_pool_closed { true };
        //bool vectorComplete { false };
        //bool consensusAchieved { false };
        //bool blockCandidateArrived { false };
        //bool round_table_sent { false };
        
        // флаг получения списка транзакций в текущем раунде, в начале раунда сбрасывается
        bool is_trans_list_recv;
        
        //bool vectorReceived { false };
        //bool gotBlockThisRound { false };
        //bool writingConfirmationGot { false };
        //bool gotBigBang { false };
        //bool writingConfGotFrom [100];
        //uint8_t writingCongGotCurrent { 0 };
        //bool allMatricesReceived { false };
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

    int32_t SolverContext::round() const
    {
        return core.cur_round;
    }

    bool SolverContext::is_spammer() const
    {
        return core.opt_spammer_on;
    }

    void SolverContext::becomeNormal()
    {
        core.handleTransitions(SolverCore::Event::SetNormal);
    }

    void SolverContext::becomeTrusted()
    {
        core.handleTransitions(SolverCore::Event::SetTrusted);
    }

    void SolverContext::becomeWriter()
    {
        core.handleTransitions(SolverCore::Event::SetWriter);
    }

    void SolverContext::vectors_completed()
    {
        core.handleTransitions(SolverCore::Event::Vectors);
    }

    void SolverContext::matrices_completed()
    {
        core.handleTransitions(SolverCore::Event::Matrices);
    }

    void SolverContext::startNewRound()
    {
        core.beforeNextRound();
        core.nextRound();
    }

    void SolverContext::makeAndSendBlock()
    {
        core.prepareBlockAndSend();
    }

    void SolverContext::makeAndSendBadBlock()
    {
        core.prepareBadBlockAndSend();
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
