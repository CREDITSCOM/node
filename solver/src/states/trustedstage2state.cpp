#include <consensus.hpp>
#include <solvercontext.hpp>
#include <states/trustedstage2state.hpp>
#include <lib/system/logger.hpp>

namespace cs {
void TrustedStage2State::on(SolverContext& context) {
  DefaultStateBehavior::on(context);
  context.init_zero(stage);
  stage.sender = (uint8_t)context.own_conf_number();
  const auto ptr = context.stage1(stage.sender);
  if (ptr == nullptr) {
    cswarning() << name() << ": stage one result not found";
  }
  else {
    stage.signatures[ptr->sender] = ptr->signature;
    stage.hashes[ptr->sender] = ptr->messageHash;
  }
  // if have already received stage-1, make possible to go further (to stage-3)
  if (!context.stage1_data().empty()) {
    cslog() << name() << ": handle early received stages-1";
    for (const auto& st : context.stage1_data()) {
      csdebug() << name() << ": stage-1 [" << (int) st.sender << "]";
      if (Result::Finish == onStage1(context, st)) {
        context.complete_stage2();
        return;
      }
    }
  }

  // 3 subsequent timeouts:
  //  - request stages-1 from origins
  //  - request stages-1 from anyone
  //  - create fake stages-1 from outbound nodes and force to next state

  SolverContext* pctx = &context;
  cslog() << name() << ": start track timeout " << Consensus::T_stage_request << " ms of stages-1 received";
  timeout_request_stage.start(
      context.scheduler(), Consensus::T_stage_request,
      // timeout #1 handler:
      [pctx, this]() {
        cslog() << name() << ": timeout for stages-1 is expired, (now) skip make requests";
        //request_stages(*pctx);
        // start subsequent track timeout for "wide" request
        cslog() << name() << ": start subsequent track timeout " << Consensus::T_stage_request
                          << " ms to request neighbors about stages-1";
        timeout_request_neighbors.start(
            pctx->scheduler(), Consensus::T_stage_request,
            // timeout #2 handler:
            [pctx, this]() {
              cslog() << name() << ": timeout for requested stages is expired, make requests to neighbors";
              request_stages_neighbors(*pctx);
              // timeout #3 handler
              timeout_force_transition.start(
                pctx->scheduler(), Consensus::T_stage_request,
                [pctx, this]() {
                  cslog() << name() << ": timeout for transition is expired, mark silent nodes as outbound";
                  mark_outbound_nodes(*pctx);
                },
                true/*replace if exists*/);
            },
            true /*replace if exists*/);
      },
      true /*replace if exists*/);
}

void TrustedStage2State::off(SolverContext& /*context*/) {
  if (timeout_request_stage.cancel()) {
    cslog() << name() << ": cancel track timeout of stages-1";
  }
  if (timeout_request_neighbors.cancel()) {
    cslog() << name() << ": cancel track timeout to request neighbors about stages-1";
  }
  if(timeout_force_transition.cancel()) {
    cslog() << name() << ": cancel track timeout to force transition to next state";
  }
}

Result TrustedStage2State::onStage1(SolverContext& context, const cs::StageOne& st) {
  stage.signatures[st.sender] = st.signature;
  stage.hashes[st.sender] = st.messageHash;
  if (context.enough_stage1()) {
    cslog() << name() << ": enough stage-1 received";
    /*signing of the second stage should be placed here*/
    cslog() << name() << ": --> stage-2 [" << (int)stage.sender << "]";
    context.add_stage2(stage, true);
    return Result::Finish;
  }
  return Result::Ignore;
}

// requests stages from corresponded nodes
void TrustedStage2State::request_stages(SolverContext& context) {
  uint8_t cnt = (uint8_t)context.cnt_trusted();
  int cnt_requested = 0;
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage1(i) == nullptr) {
      context.request_stage1(i, i);
      ++cnt_requested;
    }
  }
  if(0 == cnt_requested) {
    csdebug() << name() << ": no node to request";
  }
}

// requests stages from any available neighbor nodes
void TrustedStage2State::request_stages_neighbors(SolverContext& context) {
  const auto& stage2_data = context.stage2_data();
  uint8_t cnt = (uint8_t)context.cnt_trusted();
  int cnt_requested = 0;
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage1(i) == nullptr) {
      for (const auto& d : stage2_data) {
        if (d.sender != context.own_conf_number()) {
          context.request_stage1(d.sender, i);
          ++cnt_requested;
        }
      }
    }
  }
  if(0 == cnt_requested) {
    csdebug() << name() << ": no node to request";
  }
}

// forces transition to next stage
void TrustedStage2State::mark_outbound_nodes(SolverContext& context)
{
  uint8_t cnt = (uint8_t) context.cnt_trusted();
  for(uint8_t i = 0; i < cnt; ++i) {
    if(context.stage1(i) == nullptr) {
      // it is possible to get a transition to other state in SolverCore from any iteration, this is not a problem, simply execute method until end
      context.fake_stage1(i);
    }
  }
}

}  // namespace slv2
