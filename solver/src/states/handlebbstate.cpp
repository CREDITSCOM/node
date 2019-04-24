#include <lib/system/logger.hpp>
#include <solvercontext.hpp>
#include <states/handlebbstate.hpp>

namespace cs {

void HandleBBState::on(SolverContext& context) {
    cswarning() << name() << ": BigBang processing is implemented in Node class";
    DefaultStateBehavior::on(context);
}

}  // namespace cs
