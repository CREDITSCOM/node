#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{
    /**
     * @class   HandleBBState
     *
     * @brief   A special state to handle a Big Bang. This class cannot be inherited. Acts almost as
     *          WriteState. Currently is not functional as Node grab the BB handling
     *
     * @author  aae
     * @date    02.10.2018
     *
     * @sa  T:WriteState  
     */

    class HandleBBState final : public DefaultStateBehavior
    {
    public:

        ~HandleBBState() override
        {}

        /**
         * @fn  void final::on(SolverContext& context) override;
         *
         * @brief   Override WriteState behavior. Repeat last block when on and does not require no hashes received when on
         *
         * @author  aae
         * @date    02.10.2018
         *
         * @param [in,out]  context The context.
         */

        void on(SolverContext& context) override;

        const char * name() const override
        {
            return "Handle BB";
        }

    };

}
