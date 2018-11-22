#pragma once
#include <timeouttracking.hpp>
#include <string>
#include "defaultstatebehavior.hpp"

namespace slv2 {
/**
 * @class   WaitingState
 *
 * @brief   A waiting state. This class cannot be inherited. Waits for round info until timeout expired. On timeout
 *          starts conversation with WritingState node and "next" WaitingState node to provide new round start
 *
 * @author  Alexander Avramenko
 * @date    02.11.2018
 */

class WaitingState final : public DefaultStateBehavior {
public:
  ~WaitingState() override {
  }

  void on(SolverContext& context) override;

  void off(SolverContext& context) override;

  const char* name() const override {
    return my_name.c_str();
  }

private:
  constexpr static const char* prefix = "Waiting";
  uint8_t writing_queue_num{0};
  std::string my_name{prefix};

  TimeoutTracking timeout_round;

  void activate_new_round(SolverContext& context);
};
}  // namespace slv2
