#include "DefaultStateBehavior.h"
#include "../SolverContext.h"
#include "../Consensus.h"
#include "../Blockchain.h"
#include <csdb/pool.h>
#include <lib/system/logger.hpp>

#include <algorithm>

// provide find by sequence() capability
namespace std
{
    bool operator==(const std::pair<csdb::Pool, cs::PublicKey>& lhs, uint64_t rhs)
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

    Result DefaultStateBehavior::onBlock(SolverContext& context, csdb::Pool& block, const cs::PublicKey& /*sender*/)
    {
//#ifdef MONITOR_NODE
//        addTimestampToPool(block);
//#endif
        auto g_seq = block.sequence();
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": block received #" << block.sequence() << ", " << block.transactions_count() << " transactions");
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
                // по логике солвера-1 Writer & Main отправку хэша не делают,
                // для Writer'а вопрос решен автоматически на уровне Node (он не получает блок вообще) и на уровне WriteState (он переопределяет пустой метод onBlock()),
                // а вот для Main (CollectState) ситуация не очень удобная, приходится не делать здесь отправку хэша.
                // Я переопределил методы onBlock() в NormalState & TrustedState, где по возврату значения Finish выполняю отправку хэша полученного блока
                // (т.е. получилось некоторое дублирование одинакового кода в наследниках).
                // CollectSate и прочие не переопределяют метод OnBlock(), соотвественно блок сохраняют, но не отправляют хэш обратно.
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

    Result DefaultStateBehavior::onVector(SolverContext& /*context*/, const cs::HashVector& /*vect*/, const cs::PublicKey& /*sender*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": vector ignored in this state");
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onMatrix(SolverContext& /*context*/, const cs::HashMatrix& /*matr*/, const cs::PublicKey& /*sender*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": matrix ignored in this state");
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onHash(SolverContext& /*context*/, const cs::Hash& /*hash*/, const cs::PublicKey& /*sender*/)
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

    void DefaultStateBehavior::sendLastWrittenHash(SolverContext& context, const cs::PublicKey& target)
    {
        std::string hash_val((char*) (context.blockchain().getLastWrittenHash().to_binary().data()), 32);
/*
        if(Consensus::Log) {
            constexpr const size_t hash_len = sizeof(hash_val.str) / sizeof(hash_val.str[0]);
            LOG_NOTICE(name() << ": sending hash " << byteStreamToHex(hash_val.str, hash_len) << " in reply to block sender");
        }

        context.send_hash(hash_val, target);*/ // vshilkin
    }

} // slv2
