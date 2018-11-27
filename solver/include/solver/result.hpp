#pragma once

namespace slv2 {
/**
 * @enum    Result
 *
 * @brief   Values that represent results, special values to return from INodeState methods. Handled by SolverCore.
 */

enum class Result {

  ///< State completed, SolverCore has to make a transition to another state
  Finish,

  ///< SolverCore has to ignore this event, preventing transition to another state
  Ignore,

  ///< Critical error in state, SolverCore has to make a transition immediately if it is set, otherwise act as ignore
  ///< value
  Failure,

  ///< State must implement handler for the event but it has not done yet
  NotImplemented
};
}  // namespace slv2
