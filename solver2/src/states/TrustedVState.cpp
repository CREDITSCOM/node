#include "TrustedVState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void TrustedVState::on(SolverContext& context)
    {
        // makes initial tests:
        TrustedState::on(context);
    }

    Result TrustedVState::onVector(SolverContext & context, const Credits::HashVector & vect, const PublicKey & sender)
    {
        // continue work as trusted but suppress further events on receive vectors
        TrustedState::onVector(context, vect, sender);
        return Result::Ignore;
    }
} // slv2
