#pragma once
#include "INodeState.h"

class NormalSyncState : public INodeState
{
public:

    void stateExpired(SolverCore& context) override;
    
    void onRecvBlock(SolverCore& context, const Block& block) override;

    void onRecvRoundTable(SolverCore& context, const RoundTable& table) override;

    void onRecvVector(SolverCore& context, const Vector& vector) override;

    void onRecvMatrix(SolverCore& context, const Matrix& matrix) override;

    const char * getName() const override
    {
        return "NormalSync";
    }
};
