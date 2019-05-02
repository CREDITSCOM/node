#pragma once

#include "defaultstatebehavior.hpp"

namespace cs {
/**
 * @class   SyncState
 *
 * @brief   A synchronize state. Used for node normal mode when some blocks are missed. This
 *          class cannot be inherited. Currently is not functional as Node grab the block sync
 *
 * @author  Alexander Avramenko
 * @date    09.10.2018
 *
 * @sa  T:DefaultStateBehavior
 */

class SyncState final : public DefaultStateBehavior {
public:
    ~SyncState() override {
    }

    void on(SolverContext& context) override;

    const char* name() const override {
        return "Sync";
    }
};

}  // namespace cs
