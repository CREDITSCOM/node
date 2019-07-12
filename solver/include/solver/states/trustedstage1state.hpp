#pragma once

#include <stage.hpp>
#include "defaultstatebehavior.hpp"
//#include <timeouttracking.hpp>

#include <csdb/pool.hpp>
#include <csnode/itervalidator.hpp>

#include <memory>

namespace cs {
class TransactionsPacket;
class IterValidator;
}  // namespace cs

namespace cs {
/**
 * @class   TrustedStage1State
 *
 * @brief   TODO:
 *
 * @author  Alexander Avramenko
 * @date    09.10.2018
 *
 * @sa  T:DefaultStateBehavior
 *
 * ### remarks  Aae, 30.09.2018.
 */

class TrustedStage1State : public DefaultStateBehavior {
public:
    ~TrustedStage1State() override {
    }

    void on(SolverContext& context) override;

    void off(SolverContext& context) override;

    Result onSyncTransactions(SolverContext& context, cs::RoundNumber round) override;

    Result onHash(SolverContext& context, const csdb::PoolHash& pool_hash, const cs::PublicKey& sender) override;

    const char* name() const override {
        return "Trusted-1";
    }

protected:
    bool enough_hashes{false};
    bool transactions_checked{false};
    bool min_time_expired{false};
    void finalizeStage(SolverContext& context);

    // TimeoutTracking min_time_tracking;

    cs::StageOne stage;

    cs::Hash build_vector(SolverContext& context, TransactionsPacket& trans_pack, cs::Packets& smartsPackets);
    cs::Hash formHashFromCharacteristic(const cs::Characteristic& characteristic);

    std::unique_ptr<IterValidator> pValidator_;
};

}  // namespace cs
