#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{

    class HandleBBState final : public DefaultStateBehavior
    {
    public:

        ~HandleBBState() override
        {}

        void on(SolverContext& context) override;

        //TODO: ����������� �� ������ �������-1 � �������� ����� (��. gotBlock())

        const char * name() const override
        {
            return "Handle BB";
        }

    };

}
