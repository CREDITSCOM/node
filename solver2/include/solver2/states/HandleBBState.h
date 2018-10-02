#pragma once
#include "WriteState.h"

namespace slv2
{
    /**
     * @class   HandleBBState
     *
     * @brief   A special state to handle a Big Bang. This class cannot be inherited. Acts almost as WriteState
     *
     * @author  aae
     * @date    02.10.2018
     *
     * @sa  T:DefaultStateBehavior  
     *
     * ### remarks  Aae, 30.09.2018.
     */

    class HandleBBState final : public WriteState
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
