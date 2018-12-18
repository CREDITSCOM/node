#pragma once
#include "inodestate.hpp"

namespace cs {
/**
 * @class   NoState
 *
 * @brief   A special state of "No state". Used at the very beginning of work. This class cannot
 *          be inherited. Does nothing when activated
 *
 * @author  Alexander Avramenko
 * @date    09.10.2018
 *
 * @sa  T:INodeState
 *
 * ### remarks  Aae, 30.09.2018.
 */

class NoState final : public INodeState {
public:
  void on(SolverContext& /*context*/) override {
  }

  void off(SolverContext& /*context*/) override {
  }

  void expired(SolverContext& /*context*/) override {
  }

  void onRoundEnd(SolverContext& /*context*/, bool /*is_bingbang*/) override {
  }

  /**
   * @fn  Result final::onRoundTable(SolverContext& , const cs::RoundNumber ) override
   *
   * @brief   Handles the round table action
   *
   * @author  Alexander Avramenko
   * @date    09.10.2018
   *
   * @param [in,out]  parameter1  The first parameter.
   * @param           uint32_t    The 32 t.
   *
   * @return  A Result::Finish to allow/initiate transition to proper state.
   */

  Result onRoundTable(SolverContext& /*context*/, const cs::RoundNumber /*round*/) override {
    return Result::Finish;
  }

  Result onBlock(SolverContext& /*context*/, csdb::Pool& /*pool*/, const cs::PublicKey& /*sender*/) override {
    return Result::Failure;
  }

  Result onHash(SolverContext& /*context*/, const csdb::PoolHash& /*pool_hash*/,
                const cs::PublicKey& /*sender*/) override {
    return Result::Failure;
  }

  Result onTransaction(SolverContext& /*context*/, const csdb::Transaction& /*trans*/) override {
    return Result::Failure;
  }

  Result onSyncTransactions(SolverContext& /*context*/, cs::RoundNumber /*round*/) override {
    return Result::Failure;
  }

  Result onStage1(SolverContext& /*context*/, const cs::StageOne& /*stage*/) override {
    return Result::Failure;
  }

  Result onStage2(SolverContext& /*context*/, const cs::StageTwo& /*stage*/) override {
    return Result::Failure;
  }

  Result onStage3(SolverContext& /*context*/, const cs::StageThree& /*stage*/) override {
    return Result::Failure;
  }

  const char* name() const override {
    return "None";
  }
};

}  // namespace slv2
