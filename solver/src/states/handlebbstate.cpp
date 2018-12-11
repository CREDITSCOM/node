#include <solvercontext.hpp>
#include <states/handlebbstate.hpp>
#include <lib/system/logger.hpp>

namespace cs {

void HandleBBState::on(SolverContext& context) {
  cswarning() << name() << ": BigBang processing is implemented in Node class";
  DefaultStateBehavior::on(context);
}

}  // namespace slv2
