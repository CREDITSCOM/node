#include "TrustedVState.h"
#include "../SolverContext.h"

namespace slv2
{
    void TrustedVState::on(SolverContext& context)
    {
        // makes initial tests:
        TrustedState::on(context);
    }

    Result TrustedVState::onVector(SolverContext & context, const cs::HashVector & vect, const cs::PublicKey & sender)
    {
        // continue work as trusted but suppress further events on receive vectors
        TrustedState::onVector(context, vect, sender);
        return Result::Ignore;
    }
} // slv2
