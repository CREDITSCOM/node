#include <states/trustedstage2state.hpp>
#include <solvercontext.hpp>
#include <consensus.hpp>

#include <lib/system/logger.hpp>
#include <csnode/conveyer.hpp>

namespace cs {

#define TIMER_BASE_ID 20

void TrustedStage2State::on(SolverContext& context) {
    DefaultStateBehavior::on(context);
    cnt_recv_stages = 0;
    context.init_zero(stage);
    stage.sender = context.own_conf_number();
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
        csdebug() << name() << ": handle early received stages-1";
        bool finish = false;
        for (const auto& st : context.stage1_data()) {
            csdebug() << name() << ": stage-1 [" << static_cast<int>(st.sender) << "] has already received";
            if (Result::Finish == onStage1(context, st)) {
                finish = true;
            }
        }
        if (finish) {
            context.complete_stage2();
            return;
        }
    }

    // 3 subsequent timeouts:
    //  - request stages-1 from origins
    //  - request stages-1 from anyone
    //  - create fake stages-1 from outbound nodes and force to next state

    constexpr size_t TimerBaseId = 20;
    csunused(TimerBaseId);

    SolverContext* pctx = &context;

    auto dt = Consensus::T_stage_request;
    // increase dt in case of large trx amount:
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    const cs::Characteristic * characteristic = conveyer.characteristic(conveyer.currentRoundNumber());
    if (characteristic != nullptr) {
        // count of transactions seen in build_vector on stage-1
        size_t cnt_trx = characteristic->mask.size();
        if (cnt_trx > dt) {
            dt = cnt_trx; // 1 msec/transaction, 5K trx => 5 sec timeout
        }
    }
    csdebug() << name() << ": start track timeout " << 0 << " ms of stages-1 received";
    timeout_request_stage.start(context.scheduler(), 0,
                                // timeout #1 handler:
                                [pctx, this, dt]() {
                                    csdebug() << name() << ": (now) skip direct requests for absent stages-1";
                                    // request_stages(*pctx);
                                    // start subsequent track timeout for "wide" request
                                    csdebug() << name() << ": start subsequent track timeout " << dt << " ms to request neighbors about stages-1";
                                    timeout_request_neighbors.start(pctx->scheduler(), dt,
                                                                    // timeout #2 handler:
                                                                    [pctx, this, dt]() {
                                                                        csdebug() << name() << ": timeout for requested stages is expired, make requests to neighbors";
                                                                        request_stages_neighbors(*pctx);
                                                                        // timeout #3 handler
                                                                        csdebug() << name() << ": start subsequent track timeout " << dt << " ms to mark silent nodes";
                                                                        timeout_force_transition.start(
                                                                            pctx->scheduler(), dt,
                                                                            [pctx, this, dt]() {
                                                                                csdebug() << name() << ": timeout for transition is expired, mark silent nodes as outbound";
                                                                                mark_outbound_nodes(*pctx);
                                                                            },
                                                                            true /*replace if exists*/, TIMER_BASE_ID + 3);
                                                                    },
                                                                    true /*replace if exists*/, TIMER_BASE_ID + 2);
                                },
                                true /*replace if exists*/, TIMER_BASE_ID + 1);
}

void TrustedStage2State::off(SolverContext& /*context*/) {
    if (timeout_request_stage.cancel()) {
        csdebug() << name() << ": cancel track timeout of stages-1";
    }
    if (timeout_request_neighbors.cancel()) {
        csdebug() << name() << ": cancel track timeout to request neighbors about stages-1";
    }
    if (timeout_force_transition.cancel()) {
        csdebug() << name() << ": cancel track timeout to force transition to next state";
    }
}

Result TrustedStage2State::onStage1(SolverContext& context, const cs::StageOne& st) {
    stage.signatures[st.sender] = st.signature;
    stage.hashes[st.sender] = st.messageHash;
    ++cnt_recv_stages;
    if (cnt_recv_stages == context.cnt_trusted()) {
        csdebug() << name() << ": enough stage-1 received";
        /*signing of the second stage should be placed here*/
        csdebug() << name() << ": --> stage-2 [" << static_cast<int>(stage.sender) << "]";
        context.add_stage2(stage, true);
        return Result::Finish;
    }
    return Result::Ignore;
}

// requests stages from corresponded nodes
void TrustedStage2State::request_stages(SolverContext& context) {
    const uint8_t cnt = static_cast<uint8_t>(context.cnt_trusted());
    int cnt_requested = 0;
    for (uint8_t i = 0; i < cnt; ++i) {
        if (context.stage1(i) == nullptr) {
            context.request_stage1(i, i);
            ++cnt_requested;
        }
    }
    if (!cnt_requested) {
        csdebug() << name() << ": no node to request";
    }
}

// requests stages from any available neighbor nodes
void TrustedStage2State::request_stages_neighbors(SolverContext& context) {
    const auto& stage2_data = context.stage2_data();
    const uint8_t cnt = static_cast<uint8_t>(context.cnt_trusted());
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
    if (0 == cnt_requested) {
        csdebug() << name() << ": no node to request";
    }
}

// forces transition to next stage
void TrustedStage2State::mark_outbound_nodes(SolverContext& context) {
    const uint8_t cnt = static_cast<uint8_t>(context.cnt_trusted());
    for (uint8_t i = 0; i < cnt; ++i) {
        if (context.stage1(i) == nullptr) {
            // it is possible to get a transition to other state in SolverCore from any iteration, this is not a problem,
            // simply execute method until end
            context.fake_stage1(i);
        }
    }
}

}  // namespace cs
