#include "TrustedWriteState.h"
#include "SolverCore.h"
#include <iostream>

void TrustedWriteState::stateExpired(SolverCore& context)
{
    context.Finish();
}

void TrustedWriteState::onRecvBlock(SolverCore& context, const Block& block)
{
    std::cout << getName() << " block received: " << block.SequenceNumber() << std::endl;
}

void TrustedWriteState::onRecvRoundTable(SolverCore& context, const RoundTable& table)
{
    std::cout << getName() << " round table received: " << table.RoundNumber() << std::endl;
}

void TrustedWriteState::onRecvVector(SolverCore& context, const Vector& vector)
{
    std::cout << getName() << " vector received" << std::endl;
}

void TrustedWriteState::onRecvMatrix(SolverCore& context, const Matrix& matrix)
{
    std::cout << getName() << " matrix received" << std::endl;
}
