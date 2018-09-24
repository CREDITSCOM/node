#include "NormalState.h"
#include "SolverCore.h"
#include <iostream>

void NormalState::stateExpired(SolverCore& context)
{
    context.setNormalSyncState();
}

void NormalState::onRecvBlock(SolverCore& context, const Block& block)
{
    std::cout << getName() << " block received: " << block.SequenceNumber() << std::endl;
}

void NormalState::onRecvRoundTable(SolverCore& context, const RoundTable& table)
{
    std::cout << getName() << " round table received: " << table.RoundNumber() << std::endl;
}

void NormalState::onRecvVector(SolverCore& context, const Vector& vector)
{
    std::cout << getName() << " vector received" << std::endl;
}

void NormalState::onRecvMatrix(SolverCore& context, const Matrix& matrix)
{
    std::cout << getName() << " matrix received" << std::endl;
}
