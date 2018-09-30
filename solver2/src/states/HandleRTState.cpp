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
                context.becomeTrusted();
                break;
            case NodeLevel::Normal:
                context.becomeNormal();
                break;
            case NodeLevel::Writer:
                std::cout << name() << " warning: node must not become writer through round table" << std::endl;
                context.becomeWriter();
                break;
            case NodeLevel::Main:
            default:
                std::cout << name() << " error: unexpected NodeLevel() result from Node" << std::endl;
                break;
        }
    }

} // slv2
