#include "DefaultStateBehavior.h"
#include "../SolverContext.h"
#include "../Node.h"

#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
#endif
#include <lib/system/logger.hpp>

#include <algorithm>

// provide find by sequence() capability
namespace std
{
    bool operator==(const std::pair<csdb::Pool, PublicKey>& lhs, uint64_t rhs)
    {
        return lhs.first.sequence() == rhs;
    }
}

namespace slv2
{

    Result DefaultStateBehavior::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": round table received (#" << round << ")");
        }
        return Result::Finish;
    }

    Result DefaultStateBehavior::onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& /*sender*/)
    {
//#ifdef MONITOR_NODE
//        addTimestampToPool(block);
//#endif
        auto g_seq = block.sequence();
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": block received (#" << block.sequence() << ", " << block.transactions_count() << " transactions)");
        }
        if(g_seq > context.round()) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": block sequence number is out of current round " << context.round());
            }
            // remove this when the block candidate signing of all trusted will be implemented
            return Result::Ignore;
        }
        context.blockchain().setGlobalSequence(static_cast<uint32_t>(g_seq));
        auto awaiting_seq = context.blockchain().getLastWrittenSequence() + 1;
        if(g_seq == awaiting_seq ) {
            if(block.verify_signature()) {
                context.store_received_block(block);
                // �� ������ �������-1 Writer & Main �������� ���� �� ������,
                // ��� Writer'� ������ ����� ������������� �� ������ Node (�� �� �������� ���� ������) � �� ������ WriteState (�� �������������� ������ ����� onBlock()),
                // � ��� ��� Main (CollectState) �������� �� ����� �������, ���������� �� ������ ����� �������� ����.
                // � ������������� ������ onBlock() � NormalState & TrustedState, ��� �� �������� �������� Finish �������� �������� ���� ����������� �����
                // (�.�. ���������� ��������� ������������ ����������� ���� � �����������).
                // CollectSate � ������ �� �������������� ����� OnBlock(), ������������� ���� ���������, �� �� ���������� ��� �������.
                return Result::Finish;
            }
            else {
                if(Consensus::Log) {
                    LOG_WARN(name() << ": block #" << g_seq << " has correct sequence but wrong signature, ignore");
                }
            }
        }
        else {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": only block #" << awaiting_seq << " is allowed, ignore");
            }
            
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onVector(SolverContext& /*context*/, const Credits::HashVector& /*vect*/, const PublicKey& /*sender*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": vector ignored in this state");
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onMatrix(SolverContext& /*context*/, const Credits::HashMatrix& /*matr*/, const PublicKey& /*sender*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": matrix ignored in this state");
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onHash(SolverContext& /*context*/, const Hash& /*hash*/, const PublicKey& /*sender*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": hash ignored in this state");
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onTransaction(SolverContext& /*context*/, const csdb::Transaction& /*trans*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": transactions ignored in this state");
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onTransactionList(SolverContext& /*context*/, const csdb::Pool& /*pool*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": transaction list ignored in this state");
        }
        return Result::Ignore;
    }

    void DefaultStateBehavior::sendLastWrittenHash(SolverContext& context, const PublicKey& target)
    {
        Hash hash_val((char*) (context.blockchain().getLastWrittenHash().to_binary().data()));
        if(Consensus::Log) {
            constexpr const size_t hash_len = sizeof(hash_val.str) / sizeof(hash_val.str[0]);
            LOG_NOTICE(name() << ": sending hash " << byteStreamToHex(hash_val.str, hash_len) << " in reply to block sender");
        }
        context.node().sendHash(hash_val, target);
    }

} // slv2
