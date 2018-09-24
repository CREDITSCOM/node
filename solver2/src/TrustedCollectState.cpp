#include "TrustedCollectState.h"
#include "SolverCore.h"
#include <iostream>

void TrustedCollectState::stateExpired(SolverCore& context)
{
    context.setTrustedWriteState();
}

void TrustedCollectState::onRecvBlock(SolverCore& context, const Block& block)
{
    std::cout << getName() << " block received: " << block.SequenceNumber() << std::endl;
}

void TrustedCollectState::onRecvRoundTable(SolverCore& context, const RoundTable& table)
{
    std::cout << getName() << " round table received: " << table.RoundNumber() << std::endl;
}

void TrustedCollectState::onRecvVector(SolverCore& context, const Vector& vector)
{
    std::cout << getName() << " vector received" << std::endl;
}

void TrustedCollectState::onRecvMatrix(SolverCore& context, const Matrix& matrix)
{
    std::cout << getName() << " matrix received" << std::endl;
}
