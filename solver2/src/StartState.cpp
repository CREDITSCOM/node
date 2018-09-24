#include "StartState.h"
#include "SolverCore.h"
#include <iostream>

void StartState::onRecvBlock(SolverCore& context, const Block& block)
{
    std::cout << getName() << " block received: " << block.SequenceNumber() << std::endl;
}

void StartState::onRecvRoundTable(SolverCore& context, const RoundTable& table)
{
    std::cout << getName() << " round table received: " << table.RoundNumber() << std::endl;
}

void StartState::onRecvVector(SolverCore& context, const Vector& vector)
{
    std::cout << getName() << " vector received" << std::endl;
}

void StartState::onRecvMatrix(SolverCore& context, const Matrix& matrix)
{
    std::cout << getName() << " matrix received" << std::endl;
}
