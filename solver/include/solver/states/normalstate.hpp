#pragma once

#include <callsqueuescheduler.hpp>
#include <csdb/address.hpp>
#include <vector>
#include "defaultstatebehavior.hpp"

namespace cs {
/**
 * @class   NormalState
 *
 * @brief   A normal node state. If spammer mode is on in SolverCore, this state implements spam functionality
 *
 * @author  Alexander Avramenko
 * @date    09.10.2018
 *
 * @sa  T:DefaultStateBehavior
 *
 * ### remarks  Aae, 30.09.2018.
 */

class NormalState : public DefaultStateBehavior {
public:
    ~NormalState() override {
    }

    void on(SolverContext& context) override;

    const char* name() const override {
        return "Normal";
    }
};

}  // namespace cs
