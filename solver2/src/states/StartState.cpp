#include "StartState.h"
#include "../SolverCore.h"

#if !defined(SOLVER_USES_PROXY_TYPES)
#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)
#endif

#include <iostream>

namespace slv2
{
    void StartState::on(SolverContext& /*context*/)
    {
        //TODO: проверить, что
        // на практике не вызывается на первом раунде, как м.б. подумать
//
//        context.node().becomeWriter(); //???
//#ifdef SPAM_MAIN
//        createSpam = false;
//        spamThread.join();
//        prepareBlockForSend(testPool);
//        node_->sendBlock(testPool);
//#else
//        context.makeAndSendBlock();
//        context.makeAndSendBadBlock();
//#endif
    }

    Result StartState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

} // slv2
