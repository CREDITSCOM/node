#include "DefaultStateBehavior.h"
#include "../SolverContext.h"
#include "../Node.h"

#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
#endif

#include <algorithm>
#include <iostream>

// provide find by sequence() capability
namespace std
{
    bool operator==(const csdb::Pool& lhs, uint64_t rhs)
    {
        return lhs.sequence() == rhs;
    }
}

namespace slv2
{

    Result DefaultStateBehavior::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        if(Consensus::Log) {
            std::cout << name() << ": round table received (#" << round << ")" << std::endl;
        }
        return Result::Finish;
    }

    void DefaultStateBehavior::try_blocks_in_cahce(SolverContext& context, uint64_t last_seq)
    {
        if(!future_blocks.empty()) {
            auto ibegin = last_seq + 1;
            auto iend = ibegin + future_blocks.size();
            for(auto i = ibegin; i < iend; ++i) {
                // until first absent number reached
                auto it = std::find(future_blocks.begin(), future_blocks.end(), i);
                if(it == future_blocks.end()) {
                    // not consequent blocks in cache
                    break;
                }
                // we can add block to chain
                csdb::Pool& b = *it;
                if(b.verify_signature()) {
                    context.node().getBlockChain().putBlock(b);
                    if(Consensus::Log) {
                        std::cout << name() << ": block #" << i << " stored from cache" << std::endl;
                    }
                    future_blocks.erase(it);
                    last_seq = i;
                }
                else {
                    if(Consensus::Log) {
                        std::cout << name() << ": block #" << i << " is in cache but has wrong signature, ignored" << std::endl;
                    }
                    break;
                }
                // last_seq includes newly inserted blocks, remove outdated blocks if any
                for(auto itc = future_blocks.cbegin(); itc != future_blocks.cend(); ++itc) {
                    if(itc->sequence() <= last_seq) {
                        itc = future_blocks.erase(itc);
                        if(itc == future_blocks.cend()) {
                            break;
                        }
                    }
                }
            }
        }
    }

    Result DefaultStateBehavior::onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& /*sender*/)
    {
//#ifdef MONITOR_NODE
//        addTimestampToPool(block);
//#endif
        auto g_seq = block.sequence();
        if(Consensus::Log) {
            std::cout << name() << ": block received (#" << block.sequence() << ", " << block.transactions_count() << " transactions)" << std::endl;
        }
        if(g_seq > context.round()) {
            if(Consensus::Log) {
                std::cout << name() << ": block sequence number is out of current round " << context.round() << std::endl;
            }
            // remove this when the block candidate signing of all trusted will be implemented
            return Result::Ignore;
        }
        context.node().getBlockChain().setGlobalSequence(static_cast<uint32_t>(g_seq));
        auto awaiting_seq = context.node().getBlockChain().getLastWrittenSequence() + 1;
        if(g_seq == awaiting_seq ) {
            if(block.verify_signature()) {
                context.node().getBlockChain().putBlock(block);
                // test future blocks and insert appropriate ones
                try_blocks_in_cahce(context, g_seq);
                // по логике солвера-1 Writer & Main отправку хэша не делают,
                // дл€ Writer'а вопрос решен автоматически на уровне Node (он не получает блок вообще) и на его уровне (он переопредел€ет пустой метод onBlock()),
                // а вот дл€ Main (CollectState) ситуаци€ не очень удобна€,
                // € переопределил методы onBlock() в NormalState & TrustedState, где по возврату значени€ Finish выполн€ю отправку хэша полученного блока
                // (т.е. получилось не очень большое дублирование одинакового кода в переопределенных методах)
                return Result::Finish;
            }
            else {
                if(Consensus::Log) {
                    std::cout << name() << ": block has correct #sequence but wrong signature, ignore" << std::endl;
                }
            }
        }
        else {
            if(Consensus::Log) {
                std::cout << name() << ": only block #" << awaiting_seq << " is allowed, ignore" << std::endl;
                // store future blocks in cache
                if(g_seq > awaiting_seq) {
                    future_blocks.push_back(block);
                }
            }
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onVector(SolverContext& /*context*/, const Credits::HashVector& /*vect*/, const PublicKey& /*sender*/)
    {
        if(Consensus::Log) {
            std::cout << name() << ": vector ignored" << std::endl;
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onMatrix(SolverContext& /*context*/, const Credits::HashMatrix& /*matr*/, const PublicKey& /*sender*/)
    {
        if(Consensus::Log) {
            std::cout << name() << ": matrix ignored" << std::endl;
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onHash(SolverContext& /*context*/, const Hash& /*hash*/, const PublicKey& /*sender*/)
    {
        if(Consensus::Log) {
            std::cout << name() << ": hash ignored" << std::endl;
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onTransaction(SolverContext& /*context*/, const csdb::Transaction& /*trans*/)
    {
        if(Consensus::Log && report_ignore_transactions) {
            report_ignore_transactions = false;
            std::cout << name() << ": transactions ignored in this state" << std::endl;
        }
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onTransactionList(SolverContext& /*context*/, const csdb::Pool& /*pool*/)
    {
        if(Consensus::Log) {
            std::cout << name() << ": transaction list ignored" << std::endl;
        }
        return Result::Ignore;
    }

} // slv2
