#pragma once
#include <callsqueuescheduler.hpp>
#include <lib/system/keys.hpp>
#include <vector>
#include "defaultstatebehavior.hpp"

namespace cs {
/**
 * @class   PermanentCollectWriteState
 *
 * @brief   A permanent collect write state. This class cannot be inherited. It is used in special testing mode to serve
 * at the same time as Collector (main node) and Writer (write node) during the same round
 *
 * @author  Alexander Avramenko
 * @date    11.10.2018
 */

class PrimitiveWriteState final : public DefaultStateBehavior {
public:
  ~PrimitiveWriteState() override {
  }

  void on(SolverContext& context) override;

  void off(SolverContext& context) override;

  Result onHash(SolverContext& context, const csdb::PoolHash& pool_hash, const cs::PublicKey& sender) override;

  Result onSyncTransactions(SolverContext& context, cs::RoundNumber round) override;

  const char* name() const override {
    return "Primitive Write";
  }

private:
  CallsQueueScheduler::CallTag tag_timeout{CallsQueueScheduler::no_tag};

  std::vector<cs::PublicKey> trusted_candidates;
};

}  // namespace slv2
