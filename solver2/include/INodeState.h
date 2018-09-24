#pragma once
#include "ProxiTypes.h"

class SolverCore;

class INodeState
{
public:

    virtual void stateOn(SolverCore& /*context*/, const INodeState& /*prevState*/)
    {}

    virtual void stateOff(SolverCore& /*context*/)
    {}

    virtual void stateExpired(SolverCore& /*context*/)
    {}

    virtual void onRecvBlock(SolverCore& context, const Block& block) = 0;

    virtual void onRecvRoundTable(SolverCore& context, const RoundTable& table) = 0;

    virtual void onRecvVector(SolverCore& context, const Vector& vector) = 0;

    virtual void onRecvMatrix(SolverCore& context, const Matrix& matrix) = 0;

    virtual const char * getName() const = 0;
};
