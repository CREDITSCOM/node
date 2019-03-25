#ifndef TRANSACTIONS_VALIDATOR_HPP
#define TRANSACTIONS_VALIDATOR_HPP

#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <lib/system/common.hpp>
#include <limits>
#include <csnode/walletsstate.hpp>
#include <vector>

namespace cs {

class SolverContext;

class TransactionsValidator {
public:
  using Transactions = std::vector<csdb::Transaction>;
  using CharacteristicMask = cs::Bytes;
  using TransactionIndex = WalletsState::TransactionIndex;
  using RejectedSmarts = std::vector<std::pair<csdb::Transaction, size_t>>;

public:
  struct Config {
    size_t initialNegNodesNum_ = 2 * 1024 * 1024;
  };

public:
  TransactionsValidator(WalletsState& walletsState, const Config& config);

  void reset(size_t transactionsNum);
  bool validateTransaction(SolverContext& context, const Transactions& trxs, size_t trxInd);
  void checkRejectedSmarts(SolverContext& context, const Transactions& trxs, CharacteristicMask& maskIncluded);
  void validateByGraph(CharacteristicMask& maskIncluded, const Transactions& trxs, csdb::Pool& trxsExcluded);
  size_t getCntRemovedTrxs() const {
    return cntRemovedTrxs_;
  }

private:
  using TrxList = std::vector<TransactionIndex>;
  using Node = WalletsState::WalletData;
  using Stack = std::vector<Node*>;
  static constexpr csdb::Amount zeroBalance_ = 0.0_c;

private:
  void makeSmartsValid(SolverContext& context, RejectedSmarts& smarts,
                       const csdb::Address& source, CharacteristicMask& maskIncluded);
  bool validateTransactionAsSource(SolverContext& context, const Transactions& trxs,
                                   size_t trxInd);
  bool validateNewStateAsSource(SolverContext& context, const csdb::Transaction& trx);
  bool validateCommonAsSource(SolverContext& context, const Transactions& trxs, size_t trxInd,
                              WalletsState::WalletData& wallState);

  bool validateTransactionAsTarget(const csdb::Transaction& trx);

  void removeTransactions(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded,
                          csdb::Pool& trxsExcluded);
  bool removeTransactions_PositiveOne(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded,
                                      csdb::Pool& trxsExcluded);
  bool removeTransactions_PositiveAll(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded,
                                      csdb::Pool& trxsExcluded);
  bool removeTransactions_NegativeOne(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded,
                                      csdb::Pool& trxsExcluded);
  bool removeTransactions_NegativeAll(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded,
                                      csdb::Pool& trxsExcluded);

private:
  Config config_;

  WalletsState& walletsState_;
  TrxList trxList_;

  std::map<csdb::Address, csdb::Amount> payableMaxFees_;
  std::vector<csdb::Address> rejectedNewStates_;
  Stack negativeNodes_;
  size_t cntRemovedTrxs_;
};
}  // namespace cs

#endif
