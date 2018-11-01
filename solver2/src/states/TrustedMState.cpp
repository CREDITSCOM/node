#include "TrustedMState.h"
#include "../SolverContext.h"

namespace slv2
{
    void TrustedMState::on(SolverContext& context)
    {
        // makes initial tests
        TrustedState::on(context);
    }

    Result TrustedMState::onMatrix(SolverContext & context, const cs::HashMatrix & matr, const cs::PublicKey & sender)
    {
        // continue work as trusted but suppress further events on receive matrices
        TrustedState::onMatrix(context, matr, sender);
        return Result::Ignore;
    }

} // slv2
