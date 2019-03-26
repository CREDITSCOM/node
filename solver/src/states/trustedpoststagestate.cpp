#include <solvercontext.hpp>
#include <states/trustedpoststagestate.hpp>
#include <lib/system/logger.hpp>
#include <csnode/conveyer.hpp>

namespace cs {

#define TIMER_BASE_ID 40

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
  csdebug() << name() << ": start track timeout " << 0 << " ms of stages-3 received";
  timeout_request_stage.start(
      context.scheduler(), 0,
      // timeout #1 handler:
      [pctx, this]() {
        csdebug() << name() << ": direct request for absent stages-3";
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
              csdebug() << name() << ": start subsequent track timeout " << Consensus::T_stage_request
                << " ms to give up in receiving stages-3";
              timeout_force_transition.start(
                pctx->scheduler(), Consensus::T_stage_request,
                [pctx, this]() {
                  csdebug() << name() << ": timeout for transition is expired, mark silent nodes as outbound and recalculate the signatures";
                  mark_outbound_nodes(*pctx);
                },
                true/*replace if exists*/, TIMER_BASE_ID + 3);
        },
            true /*replace if exists*/, TIMER_BASE_ID + 2);
      },
      true /*replace if exists*/, TIMER_BASE_ID + 1);
}

void TrustedPostStageState::off(SolverContext& /*context*/) {
  csdebug() << name() << ": finishing 3rd stage";
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

void TrustedPostStageState::mark_outbound_nodes(SolverContext& context) {
  cs::RoundNumber rNum = cs::Conveyer::instance().currentRoundNumber();
  csdebug() << name() << ": mark outbound nodes in round #" << rNum;
  auto cnt = static_cast<uint8_t>(context.cnt_trusted());
  cs::Bytes realTrusted = context.stage3(context.own_conf_number())->realTrustedMask;

  if(realTrusted.size() == cnt){
    for (uint8_t i = 0; i < cnt; ++i) {
      if (context.stage3(i) == nullptr) {
        // it is possible to get a transition to other state in SolverCore from any iteration, this is not a problem, simply execute method until end
        csdebug() << name() << ": making fake stage-3 [" << static_cast<int>(i) << "] in round " << rNum;
        realTrusted[i] = cs::ConfidantConsts::InvalidConfidantIndex;
        context.realTrustedSetValue(i, cs::ConfidantConsts::InvalidConfidantIndex);
        // this procedute can cause the round change
      }
    }

    for (uint8_t i = 0; i < cnt; ++i) {
      if (realTrusted[i] == cs::ConfidantConsts::InvalidConfidantIndex) {
        context.fake_stage3(i);
      }
    }
  }
  //TODO: add the code to go to the third stage -> 
}

Result TrustedPostStageState::onStage3(SolverContext& context, const cs::StageThree& /*stage*/) {
  csdebug() << name() << ": TrueStages3 amount = " <<  context.trueStagesThree() << ", realTrusted.value = " << context.cnt_real_trusted();
  if (context.trueStagesThree() == context.cnt_real_trusted()) {
    csdebug() << name() << ": enough stage-3 received amount = " << context.trueStagesThree();
    return Result::Finish;
  }
  if (context.realTrustedChanged() && context.stagesThree() == context.cnt_real_trusted()) {
    if(context.cnt_real_trusted() > context.getRealTrusted().size() / 2U) {
      csdebug() << name() << ": the number of received messages on stage 3 doesn't correspond to the signed one, we have to retry stage 3";
      return Result::Retry;
    }
    else {
      csdebug() << name() << ": there is no availability to continue this consensus - not enough stages 3 with hashes like mine";
      return Result::Failure;
    }
  }
  return Result::Ignore;
}

}  // namespace slv2
