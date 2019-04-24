#include <solvercontext.hpp>
#include <states/syncstate.hpp>

namespace cs {

void SyncState::on(SolverContext& context) {
    DefaultStateBehavior::on(context);
}

}  // namespace cs
