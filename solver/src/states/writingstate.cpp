#include <consensus.hpp>
#include <solvercontext.hpp>
#include <writingstate.hpp>

namespace cs {

void WritingState::on(SolverContext& context) {
  // simply try to spawn next round
  cslog() << name() << ": spawn next round";
  context.spawn_next_round();
}

}  // namespace slv2
