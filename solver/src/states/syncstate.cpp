#include <solvercontext.hpp>
#include <states/syncstate.hpp>

namespace slv2 {

void SyncState::on(SolverContext& context) {
  DefaultStateBehavior::on(context);
}

}  // namespace slv2
