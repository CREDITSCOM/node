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

        inline void becomeNormal();
        inline void becomeTrusted();
        inline void becomeWriter();
        inline void vectors_completed();
        inline void matrices_completed();

        // fast access methods, may be removed at the end
        inline Node& node() const;
        inline Credits::Generals& generals() const;
        inline CallsQueueScheduler& scheduler() const;

        inline const KeyType& public_key() const;
        inline const KeyType& private_key() const;
        inline const Credits::HashVector& hash_vector() const;

        inline int32_t round() const;
        uint8_t own_conf_number() const;
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
