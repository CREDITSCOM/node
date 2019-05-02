#pragma once

namespace cs {
/**
 * @enum    Result
 *
 * @brief   Values that represent results, special values to return from INodeState methods. Handled by SolverCore.
 */

enum class Result{
    ///< State completed, SolverCore has to make a transition to another state
    Finish,

    ///< SolverCore has to ignore this event, preventing transition to another state
    Ignore,

    ///< Critical error in state, SolverCore has to make a transition immediately if it is set, otherwise act as ignore
    ///< value
    Failure,

    ///< Some errors occur, but there is a possibility to retry another time to do it better
    ///< value
    Retry,

    ///< State must implement handler for the event but it has not done yet
    NotImplemented
};
}  // namespace cs
