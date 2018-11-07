#include <states/DefaultStateBehavior.h>
#include <SolverContext.h>
#include <Consensus.h>

#pragma warning(push)
//#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/blockchain.hpp>
#pragma warning(pop)

#include <csdb/pool.h>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

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

    void DefaultStateBehavior::onRoundEnd(SolverContext& context, bool /*is_bigbang*/)
    {
        // such functionality is implemented in Node::getBigBang() method, avoid duplicated actions:
        //if(context.is_block_deferred()) {
        //    if(is_bigbang) {
        //        context.drop_deferred_block();
        //    }
        //    else {
        //        context.flush_deferred_block();
        //    }
        //}
        if(context.is_block_deferred()) {
            context.flush_deferred_block();
        }
    }

    Result DefaultStateBehavior::onRoundTable(SolverContext& /*context*/, const size_t round)
    {
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": <-- round table [" << round << "]");
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
            LOG_NOTICE(name() << ": <-- block [" << block.sequence() << "] of " << block.transactions_count());
        }
        if(g_seq > context.round()) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": block sequence number is out of current round " << context.round());
            }
            return Result::Ignore;
        }
        auto awaiting_seq = context.blockchain().getLastWrittenSequence() + 1;
        if(g_seq == awaiting_seq ) {
            if(Consensus::Log) {
                const auto& pk = block.writer_public_key();
                if(pk.empty()) {
                    LOG_ERROR(name() << ": block omits writer public key");
                }
                LOG_NOTICE(name() << ": verify block with public key " << cs::Utils::byteStreamToHex(pk.data(), pk.size()) );
            }
            if(block.verify_signature()) {
                bool defer_write = (context.round() == g_seq); // outdated block need not to defer writing
                context.store_received_block(block, defer_write);
                // по логике солвера-1 Writer & Main отправку хэша не делают,
                // дл€ Writer'а вопрос решен автоматически на уровне Node (он не получает блок вообще) и на уровне WriteState (он переопредел€ет пустой метод onBlock()),
                // а вот дл€ Main (CollectState) ситуаци€ не очень удобна€, приходитс€ не делать здесь отправку хэша.
                // я переопределил методы onBlock() в NormalState & TrustedState, где по возврату значени€ Finish выполн€ю отправку хэша полученного блока
                // (т.е. получилось некоторое дублирование одинакового кода в наследниках).
                // CollectSate и прочие не переопредел€ют метод OnBlock(), соотвественно блок сохран€ют, но не отправл€ют хэш обратно.
                return Result::Finish;
            }
            else {
                if(Consensus::Log) {
                    LOG_WARN(name() << ": block [" << g_seq << "] has correct sequence but wrong signature, ignore");
                }
            }
        }
        else {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": only block [" << awaiting_seq << "] is allowed, ignore");
            }
            
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

    Result DefaultStateBehavior::onTransactionList(SolverContext& /*context*/, csdb::Pool& /*pool*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": transaction list ignored in this state");
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onStage1(SolverContext & /*context*/, const cs::StageOne & /*stage*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": consensus stage 1 ignored in this state");
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onStage2(SolverContext & /*context*/, const cs::StageTwo & /*stage*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": consensus stage 2 ignored in this state");
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onStage3(SolverContext & /*context*/, const cs::StageThree & /*stage*/)
    {
        if(Consensus::Log) {
            LOG_DEBUG(name() << ": consensus stage 3 ignored in this state");
        }
        return Result::Ignore;
    }

    void DefaultStateBehavior::sendLastWrittenHash(SolverContext& context, const cs::PublicKey& target)
    {
        const auto& tmp = context.last_block_hash();
        cs::Hash hash_val;
        std::copy(tmp.cbegin(), tmp.cend(), hash_val.begin());
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": --> hash = " << cs::Utils::byteStreamToHex(hash_val.data(), hash_val.size()));
        }
        context.send_hash(hash_val, target);
    }

} // slv2
