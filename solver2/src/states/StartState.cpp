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
        //TODO: ���������, ���
        // �� �������� �� ���������� �� ������ ������, ��� �.�. ��������
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
