#ifndef ITER_VALIDATOR_HPP
#define ITER_VALIDATOR_HPP

#include <memory>
#include <set>
#include <vector>

#include <csnode/transactionsvalidator.hpp>
#include <csnode/nodecore.hpp>
#include <lib/system/common.hpp>

namespace cs {
class SolverContext;
class WalletsState;

class IterValidator {
public:
  using Transactions = std::vector<csdb::Transaction>;

  IterValidator(WalletsState& wallets);
  Characteristic formCharacteristic(SolverContext&, Transactions&,
                                    Packets& smartsPackets);

private:
  bool validateTransactions(SolverContext&, Bytes& characteristicMask, const Transactions&);

  void checkRejectedSmarts(SolverContext&, Bytes& characteristicMask,
                           const Transactions&);

  void checkSignaturesSmartSource(SolverContext&, Packets& smartContractsPackets);
  void checkTransactionsSignatures(SolverContext& context,
                                   const Transactions& transactions,
                                   Bytes& characteristicMask,
                                   Packets& smartsPackets);
  bool checkTransactionSignature(SolverContext& context, const csdb::Transaction& transaction);

  bool deployAdditionalCheck(SolverContext& context, size_t trxInd,
                             const csdb::Transaction& transaction);

  std::unique_ptr<TransactionsValidator> pTransval_;
  std::set<csdb::Address> smartSourceInvalidSignatures_;
};
} // namespace cs
#endif // ITER_VALIDATOR_HPP
