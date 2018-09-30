#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{
    /// <summary>   A special state to handle a Big Bang. This class cannot be inherited</summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:DefaultStateBehavior"/>

    class HandleBBState final : public DefaultStateBehavior
    {
    public:

        ~HandleBBState() override
        {}

        void on(SolverContext& context) override;

        //TODO: ����������� �� ������ �������-1 � �������� ����� (��. Solver::gotBlock())

        const char * name() const override
        {
            return "Handle BB";
        }

    };

}
