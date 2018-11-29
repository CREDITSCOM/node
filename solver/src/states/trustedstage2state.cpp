#include <consensus.hpp>
#include <solvercontext.hpp>
#include <states/trustedstage2state.hpp>
#include <lib/system/logger.hpp>

namespace cs {
void TrustedStage2State::on(SolverContext& context) {
  DefaultStateBehavior::on(context);
  stage.sender = (uint8_t)context.own_conf_number();
  cs::Signature zeroSig;
  cs::Hash zeroHash;
  size_t cnt_trusted = context.cnt_trusted();
  memset(zeroSig.data(), 0, zeroSig.size());
  memset(zeroHash.data(), 0, zeroHash.size());
  stage.signatures.resize(cnt_trusted, zeroSig);
  stage.hashes.resize(cnt_trusted, zeroHash);
  const auto ptr = context.stage1(stage.sender);
  if (ptr == nullptr) {
    if (Consensus::Log) {
      LOG_WARN(name() << ": stage one result not found");
    }
  }
  else {
    stage.signatures[ptr->sender] = ptr->sig;
    stage.hashes[ptr->sender] = ptr->msgHash;
  }
  // if have already received stage-1, make possible to go further (to stage-3)
  if (!context.stage1_data().empty()) {
    cslog() << name() << ": handle early received stages-1";
    for (const auto& st : context.stage1_data()) {
      if (Result::Finish == onStage1(context, st)) {
        context.complete_stage2();
        return;
      }
    }
  }

  SolverContext* pctx = &context;
  if (Consensus::Log) {
    LOG_NOTICE(name() << ": start track timeout " << Consensus::T_stage_request << " ms of stages-1 received");
  }
  timeout_request_stage.start(
      context.scheduler(), Consensus::T_stage_request,
      // timeout handler:
      [pctx, this]() {
        if (Consensus::Log) {
          LOG_NOTICE(name() << ": timeout for stages-1 is expired, make requests");
        }
        request_stages(*pctx);
        // start subsequent track timeout for "wide" request
        if (Consensus::Log) {
          LOG_NOTICE(name() << ": start subsequent track timeout " << Consensus::T_stage_request
                            << " ms to request neighbors about stages-1");
        }
        timeout_request_neighbors.start(
            pctx->scheduler(), Consensus::T_stage_request,
            // timeout handler:
            [pctx, this]() {
              if (Consensus::Log) {
                LOG_NOTICE(name() << ": timeout for transition is expired, make requests to neighbors");
              }
              request_stages_neighbors(*pctx);
            },
            true /*replace if exists*/);
      },
      true /*replace if exists*/);
}

void TrustedStage2State::off(SolverContext& /*context*/) {
  if (timeout_request_stage.cancel()) {
    if (Consensus::Log) {
      LOG_NOTICE(name() << ": cancel track timeout of stages-1");
    }
  }
  if (timeout_request_neighbors.cancel()) {
    if (Consensus::Log) {
      LOG_NOTICE(name() << ": cancel track timeout to request neighbors about stages-1");
    }
  }
}

Result TrustedStage2State::onStage1(SolverContext& context, const cs::StageOne& st) {
  stage.signatures[st.sender] = st.sig;
  stage.hashes[st.sender] = st.msgHash;
  if (context.enough_stage1()) {
    LOG_NOTICE(name() << ": enough stage-1 received");
    /*signing of the second stage should be placed here*/
    LOG_NOTICE(name() << ": --> stage-2 [" << (int)stage.sender << "]");
    context.add_stage2(stage, true);
    return Result::Finish;
  }
  return Result::Ignore;
}

// requests stages from corresponded nodes
void TrustedStage2State::request_stages(SolverContext& context) {
  uint8_t cnt = (uint8_t)context.cnt_trusted();
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage1(i) == nullptr) {
      context.request_stage1(i, i);
    }
  }
}

// requests stages from any available neighbor nodes
void TrustedStage2State::request_stages_neighbors(SolverContext& context) {
  const auto& stage2_data = context.stage2_data();
  uint8_t cnt = (uint8_t)context.cnt_trusted();
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage1(i) == nullptr) {
      for (const auto& d : stage2_data) {
        if (d.sender != context.own_conf_number()) {
          context.request_stage1(d.sender, i);
        }
      }
    }
  }
}

}  // namespace slv2
