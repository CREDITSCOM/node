#include <states/HandleRTState.h>
#include <SolverContext.h>
#include <Consensus.h>
#include <lib/system/logger.hpp>

namespace slv2
{
    void HandleRTState::on(SolverContext& context)
    {
        auto role = context.role();
        switch(role) {
            case Role::Trusted:
                context.request_role(Role::Trusted);
                break;
            case Role::Normal:
                context.request_role(Role::Normal);
                break;
            default:
                if(Consensus::Log) {
                    LOG_ERROR(name() << ": unknown role requested");
                }
                break;
        }
    }

} // slv2
