#pragma once

#include <stage.hpp>
#include "defaultstatebehavior.hpp"

#include <csdb/pool.hpp>
#include <csnode/transactionsvalidator.hpp>

#include <memory>

namespace cs {
class TransactionsPacket;
class TransactionsValidator;
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

  cs::StageOne stage;
  std::unique_ptr<cs::TransactionsValidator> ptransval;

  /**
   * @fn  void TrustedStage1State::filter_test_signatures(SolverContext& context, cs::TransactionsPacket& p);
   *
   * @brief   Filter transactions in packet by testing signatures and remove them
   *
   * @author  Alexander Avramenko
   * @date    13.11.2018
   *
   * @param [in,out]  context The context.
   * @param [in,out]  p       A cs::TransactionsPacket to remove transactions with bad signatures.
   */

  void filter_test_signatures(SolverContext& context, cs::TransactionsPacket& p);
  bool check_transaction_signature(SolverContext& context, const csdb::Transaction& transaction);
  cs::Hash build_vector(SolverContext& context, const cs::TransactionsPacket& trans_pack);
  cs::Hash formHashFromCharacteristic(const cs::Characteristic& characteristic);
  void validateTransactions(SolverContext&, cs::Bytes& characteristicMask, const cs::TransactionsPacket&);
  void checkRejectedSmarts(SolverContext&, cs::Bytes& characteristicMask, const cs::TransactionsPacket&);
};

}  // namespace slv2
