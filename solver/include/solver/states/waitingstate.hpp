#ifndef WAITING_STAGE_HPP
#define WAITING_STAGE_HPP

#include <timeouttracking.hpp>
#include <string>
#include "defaultstatebehavior.hpp"

namespace cs {
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
    return myName_.c_str();
  }

private:
  constexpr static const char* prefix_ = "Waiting";
  const static uint32_t sendRoundTableDelayMs_ = 6000;

  uint8_t writingQueueNumber_ = { 0 };
  std::string myName_ = { prefix_ };

  TimeoutTracking roundTimeout_;

  void activate_new_round(SolverContext& context);
};
}  // namespace slv2

#endif // WAITING_STAGE_HPP
