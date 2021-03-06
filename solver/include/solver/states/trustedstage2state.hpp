#pragma once
#include <stage.hpp>
#include <timeouttracking.hpp>
#include "defaultstatebehavior.hpp"

namespace cs {
/**
 * @class   TrustedStage2State
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

class TrustedStage2State final : public DefaultStateBehavior {
public:
    ~TrustedStage2State() override {
    }

    /**
     * @fn  virtual void final::on(SolverContext& context) override;
     *
     * @brief   Sends stage-1 result
     *
     * @author  Alexander Avramenko
     * @date    26.10.2018
     *
     * @param [in,out]  context The context.
     */

    void on(SolverContext& context) override;

    void off(SolverContext& context) override;

    Result onStage1(SolverContext& context, const cs::StageOne& stage) override;

    const char* name() const override {
        return "Trusted-2";
    }

private:
    cs::StageTwo stage;

    size_t cnt_recv_stages;

    // timeout tracking

    TimeoutTracking timeout_request_stage;
    TimeoutTracking timeout_request_neighbors;
    TimeoutTracking timeout_force_transition;

    // requests stages from corresponded nodes
    void request_stages(SolverContext& context);

    // requests stages from any available neighbor nodes
    void request_stages_neighbors(SolverContext& context);

    // forces transition to next stage
    void mark_outbound_nodes(SolverContext& context);
};

}  // namespace cs
