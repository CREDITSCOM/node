#pragma once
#include "DefaultStateBehavior.h"
#include <Stage.h>
#include <TimeoutTracking.h>
#include <lib/system/keys.hpp>

#include <vector>

namespace slv2
{
    /**
     * @class   TrustedStage3State
     *
     * @brief   TODO:
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:TrustedState  
     *
     * ### remarks  Aae, 30.09.2018.
     */

    class TrustedStage3State final : public DefaultStateBehavior
    {
    public:

        ~TrustedStage3State() override
        {}

        virtual void on(SolverContext& context) override;

        void off(SolverContext& context) override;

        virtual Result onStage2(SolverContext& context, const cs::StageTwo& stage) override;

        const char * name() const override
        {
            return "Trusted-3";
        }
        void request_stages(SolverContext& context);
        void request_stages_neighbors(SolverContext& context);


    protected:
      // timeout tracking

        TimeoutTracking timeout_request_stage;
        TimeoutTracking timeout_request_neighbors;

        cs::StageThree stage;
        std::vector<cs::PublicKey> next_round_trust;

        void trusted_election(SolverContext& context);
        bool pool_solution_analysis(SolverContext& context);
        uint8_t take_urgent_decision(SolverContext& context);
    };

} // slv2
