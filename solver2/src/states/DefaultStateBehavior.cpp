#include "DefaultStateBehavior.h"
#include "../SolverCore.h"
#include "../Node.h"

#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
#endif

#include <iostream>

namespace slv2
{

    Result DefaultStateBehavior::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received (# " << round << ")" << std::endl;
        return Result::Finish;
    }

    Result DefaultStateBehavior::onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& sender)
    {
//#ifdef MONITOR_NODE
//        addTimestampToPool(block);
//#endif
        auto g_seq = block.sequence();
        //std::cout << "GOT NEW BLOCK: global sequence = " << g_seq << std::endl;
        if(g_seq > context.round()) {
            // remove this when the block candidate signing of all trusted will be implemented
            return Result::Ignore;
        }
        context.node().getBlockChain().setGlobalSequence(static_cast<uint32_t>(g_seq));
        if(g_seq == context.node().getBlockChain().getLastWrittenSequence() + 1) {
            //std::cout << "Solver -> getblock calls writeLastBlock" << std::endl;
            if(block.verify_signature()) {
                context.node().getBlockChain().putBlock(block);
//#ifndef MONITOR_NODE
                // по логике солвера-1 Writer & Main отправку хэша не делают,
                // для Writer'а вопрос решен автоматически на уровне Node (он не получает блок вообще) и на его уровне (он переопределяет пустой метод onBlock()),
                // а вот для Main (CollectState) ситуация не очень удобная, я пока не блокирую отправку хэша и при активном CollectState
                    //std::cout << "Solver -> before sending hash to writer" << std::endl;
                Hash test_hash((char*) (context.node().getBlockChain().getLastWrittenHash().to_binary().data()));
                context.node().sendHash(test_hash, sender);
                //std::cout << "SENDING HASH: " << byteStreamToHex(test_hash.str, 32) << std::endl;
//#endif
            }
        }
        std::cout << name() << ": block received (#" << block.sequence() << " of " << block.transactions_count() << " transactions)" << std::endl;
        return Result::Finish;
    }

    Result DefaultStateBehavior::onVector(SolverContext& /*context*/, const Credits::HashVector& /*vect*/, const PublicKey& /*sender*/)
    {
        std::cout << name() << ": vector ignored" << std::endl;
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onMatrix(SolverContext& /*context*/, const Credits::HashMatrix& /*matr*/, const PublicKey& /*sender*/)
    {
        std::cout << name() << ": matrix ignored" << std::endl;
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onHash(SolverContext& /*context*/, const Hash& /*hash*/, const PublicKey& /*sender*/)
    {
        std::cout << name() << ": hash ignored" << std::endl;
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onTransaction(SolverContext& /*context*/, const csdb::Transaction& /*trans*/)
    {
        std::cout << name() << ": transaction ignored" << std::endl;
        return Result::Ignore;
    }

    Result DefaultStateBehavior::onTransactionList(SolverContext& /*context*/, const csdb::Pool& /*pool*/)
    {
        std::cout << name() << ": transaction list ignored" << std::endl;
        return Result::Ignore;
    }

} // slv2
