#pragma once
#include <stage.hpp>
#include <timeouttracking.hpp>
#include "defaultstatebehavior.hpp"

#include <vector>

namespace cs {
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

class TrustedStage3State final : public DefaultStateBehavior {
public:
  ~TrustedStage3State() override {
  }

  virtual void on(SolverContext& context) override;

  void off(SolverContext& context) override;

  virtual Result onStage2(SolverContext& context, const cs::StageTwo& stage) override;

  const char* name() const override {
    return "Trusted-3";
  }

protected:
  // timeout tracking

  TimeoutTracking timeout_request_stage;
  TimeoutTracking timeout_request_neighbors;
  TimeoutTracking timeout_force_transition;
  
  size_t cnt_recv_stages;
  cs::StageThree stage;
  std::vector<cs::PublicKey> next_round_trust;
  std::vector<cs::TransactionsPacketHash> next_round_hashes;

  void trusted_election(SolverContext& context);
  bool pool_solution_analysis(SolverContext& context);
  bool take_urgent_decision(SolverContext& context);

  void request_stages(SolverContext& context);
  void request_stages_neighbors(SolverContext& context);
  // forces transition to next stage
  void mark_outbound_nodes(SolverContext& context, cs::RoundNumber round);

};

}  // namespace slv2
