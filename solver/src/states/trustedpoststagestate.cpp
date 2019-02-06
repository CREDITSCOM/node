#include <solvercontext.hpp>
#include <states/trustedpoststagestate.hpp>
#include <lib/system/logger.hpp>

namespace cs {
void TrustedPostStageState::on(SolverContext& context) {
  DefaultStateBehavior::on(context);

  cnt_recv_stages = 0;
  //// decide to write
  // const auto ptr = context.stage3(context.own_conf_number());
  // if(ptr != nullptr) {
  //    if(ptr->sender == ptr->writer) {
  //        context.request_role(Role::Writer);
  //        return;
  //    }
  //}

  // process already received stage-3, possible to go further to waiting/writting state
  if(!context.stage3_data().empty()) {
    csdebug() << name() << ": handle early received stages-3";
    bool finish = false;
    for(const auto& st : context.stage3_data()) {
      if(Result::Finish == onStage3(context, st)) {
        finish = true;
      }
    }
    if(finish) {
      context.complete_post_stage();
      return;
    }
  }

  SolverContext* pctx = &context;
  csdebug() << name() << ": start track timeout " << Consensus::T_stage_request << " ms of stages-3 received";
  timeout_request_stage.start(
      context.scheduler(), Consensus::T_stage_request,
      // timeout #1 handler:
      [pctx, this]() {
        csdebug() << name() << ": timeout for stages-3 is expired, make requests";
        request_stages(*pctx);
        // start subsequent track timeout for "wide" request
        csdebug() << name() << ": start subsequent track timeout " << Consensus::T_stage_request
                          << " ms to request neighbors about stages-3";
        timeout_request_neighbors.start(
            pctx->scheduler(), Consensus::T_stage_request,
            // timeout #2 handler:
            [pctx, this]() {
              csdebug() << name() << ": timeout for transition is expired, make requests to neighbors";
              request_stages_neighbors(*pctx);
              // timeout #3 handler
              timeout_force_transition.start(
                pctx->scheduler(), Consensus::T_stage_request,
                [pctx, this]() {
                  csdebug() << name() << ": timeout for transition is expired, cannot proceed further, BigBang required";
                },
                true/*replace if exists*/);
        },
            true /*replace if exists*/);
      },
      true /*replace if exists*/);
}

void TrustedPostStageState::off(SolverContext& /*context*/) {
  if (timeout_request_stage.cancel()) {
    csdebug() << name() << ": cancel track timeout of stages-3";
  }
  if (timeout_request_neighbors.cancel()) {
    csdebug() << name() << ": cancel track timeout to request neighbors about stages-3";
  }
  if(timeout_force_transition.cancel()) {
    csdebug() << name() << ": cancel track timeout to force transition to next state";
  }
}

// requests stages from corresponded nodes
void TrustedPostStageState::request_stages(SolverContext& context) {
  uint8_t cnt = (uint8_t)context.cnt_trusted();
  auto& realTrusted = context.stage3(context.own_conf_number())->realTrustedMask;
  if (realTrusted.size() != cnt) {
    csmeta(cserror) << ": The size of real Trusted doesn't match the size of Confidants!" ;
    return;
  }
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage3(i) == nullptr  && realTrusted.at(i) != cs::ConfidantConsts::InvalidConfidantIndex) {
      context.request_stage3(i, i);
    }
  }
}

// requests stages from any available neighbor nodes
void TrustedPostStageState::request_stages_neighbors(SolverContext& context) {
  const auto& stage3_data = context.stage3_data();
  uint8_t cnt = (uint8_t)context.cnt_trusted();
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage3(i) == nullptr) {
      for (const auto& d : stage3_data) {
        if (d.sender != context.own_conf_number()) {
          context.request_stage3(d.sender, i);
        }
      }
    }
  }
}

Result TrustedPostStageState::onStage3(SolverContext& context, const cs::StageThree& /*stage*/) {
  if(context.trueStagesThree() >= context.cnt_trusted() / 2U + 1U) {
    csdebug() << name() << ": enough stage-3 received amount = " << cnt_recv_stages;
    return Result::Finish;
  }
  if (context.stagesThree() == context.cnt_trusted()) {
    csdebug() << name() << ": there is no availability to continut this consensus - not enough stages 3 with hashes like mine";
    return Result::Failure;
  }
  return Result::Ignore;
}

}  // namespace slv2
