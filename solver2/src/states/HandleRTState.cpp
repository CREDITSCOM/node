#include "HandleRTState.h"
#include "../SolverContext.h"
#include "../Consensus.h"
#include "../Node.h"
#include <lib/system/logger.hpp>

namespace slv2
{
    void HandleRTState::on(SolverContext& context)
    {
        switch(context.node().getMyLevel()) {
            case NodeLevel::Confidant:
                context.become_trusted();
                break;
            case NodeLevel::Normal:
                context.become_normal();
                break;
            case NodeLevel::Writer:
                if(Consensus::Log) {
                    LOG_WARN(name() << ": node must not become writer through round table");
                }
                context.become_writer();
                break;
            case NodeLevel::Main:
                if(Consensus::Log) {
                    if(context.round() > 1) {
                        LOG_WARN(name() << ": node may become a main (collector) through round table only as a BB or the 1st round result");
                    }
                }
                context.become_collector();
                break;
            default:
                if(Consensus::Log) {
                    LOG_ERROR(name() << ": unexpected Node::getMyLevel() result");
                }
                break;
        }
    }

} // slv2
