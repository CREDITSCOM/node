#pragma once

#include "defaultstatebehavior.hpp"

namespace slv2 {
/**
 * @class   WritingState
 *
 * @brief   A writing state. This class cannot be inherited. Writes new block and send round info to other nodes
 *
 * @author  Alexander Avramenko
 * @date    02.11.2018
 */

class WritingState final : public DefaultStateBehavior {
public:
  ~WritingState() override {
  }

  void on(SolverContext& context) override;

  const char* name() const override {
    return "Writing";
  }
};
}  // namespace slv2
