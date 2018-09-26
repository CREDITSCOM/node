#include "HandleRTState.h"
#include "../SolverCore.h"

namespace slv2
{
    void HandleRTState::stateOn(SolverContext& context)
    {
        //TODO: deduce from round table desired node status
        static uint32_t flag = 0;
        if(++flag % 2 != 0) {
            context.becomeNormal();
        }
        else {
            context.becomeTrusted();
        }
    }

} // slv2
