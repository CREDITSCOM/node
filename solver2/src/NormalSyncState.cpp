#include "NormalSyncState.h"
#include "SolverCore.h"
#include <iostream>

void NormalSyncState::stateExpired(SolverCore& context)
{
    context.setTrustedState();
}

void NormalSyncState::onRecvBlock(SolverCore& context, const Block& block)
{
    std::cout << getName() << " block received: " << block.SequenceNumber() << std::endl;
}

void NormalSyncState::onRecvRoundTable(SolverCore& context, const RoundTable& table)
{
    std::cout << getName() << " round table received: " << table.RoundNumber() << std::endl;
}

void NormalSyncState::onRecvVector(SolverCore& context, const Vector& vector)
{
    std::cout << getName() << " vector received" << std::endl;
}

void NormalSyncState::onRecvMatrix(SolverCore& context, const Matrix& matrix)
{
    std::cout << getName() << " matrix received" << std::endl;
}
