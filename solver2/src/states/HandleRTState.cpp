#include "HandleRTState.h"
#include "../SolverContext.h"
#include "../Consensus.h"
#include <lib/system/logger.hpp>

namespace slv2
{
    void HandleRTState::on(SolverContext& context)
    {
        auto role = context.role();
        switch(role) {
            case Role::Trusted:
            case Role::Normal:
                context.request_role(role);
                break;
            case Role::Write:
                if(Consensus::Log) {
                    LOG_WARN(name() << ": node must not become writer through round table");
                }
                context.request_role(role);
                break;
            case Role::Collect:
                if(Consensus::Log) {
                    if(context.round() > 1) {
                        LOG_WARN(name() << ": node may become a main (collector) through round table only as a BB or the 1st round result");
                    }
                }
                context.request_role(role);
                break;
            default:
                if(Consensus::Log) {
                    LOG_ERROR(name() << ": unknown role requested");
                }
                break;
        }
    }

} // slv2
