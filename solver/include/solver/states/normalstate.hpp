#pragma once

#include <callsqueuescheduler.hpp>
#include <csdb/address.h>
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

  /**
   * @fn  Result NormalState::onBlock(SolverContext& context, csdb::Pool& block, const cs::PublicKey& sender) override;
   *
   * @brief   Overrides base implementation to flush block immediately
   *
   * @author  Alexander Avramenko
   * @date    26.10.2018
   *
   * @param [in,out]  context The context.
   * @param [in,out]  block   The block.
   * @param           sender  The sender.
   *
   * @return  A Result.
   */

  Result onBlock(SolverContext& context, csdb::Pool& block, const cs::PublicKey& sender) override;

  const char* name() const override {
    return "Normal";
  }
};

}  // namespace slv2
