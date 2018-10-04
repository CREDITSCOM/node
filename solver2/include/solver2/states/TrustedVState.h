#pragma once
#include "TrustedState.h"

namespace slv2
{
    /// <summary>   A trusted node state with vectors completed, but matrices still not. This class cannot be inherited </summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:TrustedState"/>

    class TrustedVState final : public TrustedState
    {
    public:

        ~TrustedVState() override
        {}

        void on(SolverContext& context) override;

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        // onMatrix() behaviour is completely implemented in TrustesState

        const char * name() const override
        {
            return "TrustedV";
        }

    };

} // slv2
