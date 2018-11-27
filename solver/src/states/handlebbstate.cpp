#include <solvercontext.hpp>
#include <states/handlebbstate.hpp>
#include <lib/system/logger.hpp>

namespace cs {

void HandleBBState::on(SolverContext& context) {
  if (context.is_block_deferred()) {
    context.drop_deferred_block();
  }
  if (Consensus::Log) {
    LOG_WARN(name() << ": BigBang processing is implemented in Node class");
  }
  DefaultStateBehavior::on(context);
}

}  // namespace slv2
