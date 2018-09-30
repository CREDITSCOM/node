#include "StartState.h"
//#include "../SolverContext.h"

namespace slv2
{
    void StartState::on(SolverContext& /*context*/)
    {
        //TODO: проверить, что на практике не вызывается на первом раунде, как м.б. подумать
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

} // slv2
