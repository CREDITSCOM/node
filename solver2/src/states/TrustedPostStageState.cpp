#include <states/TrustedPostStageState.h>
#include <SolverContext.h>
#include <lib/system/logger.hpp>

namespace slv2
{
    void TrustedPostStageState::on(SolverContext & context)
    {
        DefaultStateBehavior::on(context);

        //// decide to write
        //const auto ptr = context.stage3((uint8_t) context.own_conf_number());
        //if(ptr != nullptr) {
        //    if(ptr->sender == ptr->writer) {
        //        context.request_role(Role::Writer);
        //        return;
        //    }
        //}

        // process already received stage-3, possible to go further to waiting/writting state
        for (const auto& st : context.stage3_data()) {
          if (Result::Finish == onStage3(context, st)) {
            context.complete_post_stage();
            return;
          }
        }

        SolverContext *pctx = &context;
        if (Consensus::Log) {
          LOG_NOTICE(name() << ": start track timeout " << Consensus::T_stage_request
            << " ms of stages-3 received");
        }
        timeout_request_stage.start(
          context.scheduler(),
          Consensus::T_stage_request,
          // timeout handler:
          [pctx, this]() {
          if (Consensus::Log) {
            LOG_NOTICE(name() << ": timeout for stages-3 is expired, make requests");
          }
          request_stages(*pctx);
          // start subsequent track timeout for "wide" request
          if (Consensus::Log) {
            LOG_NOTICE(name() << ": start subsequent track timeout " << Consensus::T_stage_request
              << " ms to request neighbors about stages-3");
          }
          timeout_request_neighbors.start(
            pctx->scheduler(),
            Consensus::T_stage_request,
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

    void TrustedPostStageState::off(SolverContext & /*context*/)
    {
      if (timeout_request_stage.cancel()) {
        if (Consensus::Log) {
          LOG_NOTICE(name() << ": cancel track timeout of stages-3");
        }
      }
      if (timeout_request_neighbors.cancel()) {
        if (Consensus::Log) {
          LOG_NOTICE(name() << ": cancel track timeout to request neighbors about stages-3");
        }
      }
    }


    // requests stages from corresponded nodes
    void TrustedPostStageState::request_stages(SolverContext& context)
    {
      uint8_t cnt = (uint8_t)context.cnt_trusted();
      for (uint8_t i = 0; i < cnt; ++i) {
        if (context.stage3(i) == nullptr) {
          context.request_stage3(i, i);
        }
      }
    }

    // requests stages from any available neighbor nodes
    void TrustedPostStageState::request_stages_neighbors(SolverContext& context)
    {
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

    Result TrustedPostStageState::onStage3(SolverContext & context, const Credits::StageThree & /*stage*/)
    {
        if(context.enough_stage3()) {
            LOG_NOTICE(name() << ": enough stage-3 received");
            return Result::Finish;
        }
        return Result::Ignore;
    }



} // slv2