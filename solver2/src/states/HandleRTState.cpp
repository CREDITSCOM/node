#include "HandleRTState.h"
#include "../SolverContext.h"
#include "../Node.h"
#include <iostream>

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
                    std::cout << name() << " warning: node must not become writer through round table" << std::endl;
                }
                context.become_writer();
                break;
            case NodeLevel::Main:
                if(Consensus::Log) {
                    if(context.round() > 1) {
                        std::cout << name() << " warning: node may become a main (collector) through round table only as a BigBang or the 1st round result" << std::endl;
                    }
                }
                context.become_collector();
                break;
            default:
                if(Consensus::Log) {
                    std::cout << name() << " error: unexpected getMyLevel() result from Node" << std::endl;
                }
                break;
        }
    }

} // slv2
