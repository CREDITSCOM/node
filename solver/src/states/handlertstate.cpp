#include <consensus.hpp>
#include <solvercontext.hpp>
#include <states/handlertstate.hpp>
#include <lib/system/logger.hpp>

namespace cs {
void HandleRTState::on(SolverContext& context) {
  auto role = context.role();
  switch (role) {
    case Role::Trusted:
      context.request_role(Role::Trusted);
      break;
    case Role::Normal:
      context.request_role(Role::Normal);
      break;
    default:
      if (Consensus::Log) {
        LOG_ERROR(name() << ": unknown role requested");
      }
      break;
  }
}

}  // namespace slv2
