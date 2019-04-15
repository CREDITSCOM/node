#ifndef BLOCK_VALIDATOR_HPP
#define BLOCK_VALIDATOR_HPP

#include <memory>

#include <csnode/itervalidator.hpp>
#include <csnode/fee.hpp>
#include <csnode/walletsstate.hpp>

namespace csdb {
class Pool;
} // namespace csdb

class BlockChain;

namespace cs {

class BlockValidator {
public:
  enum ValidationLevel : uint8_t {
    noValidation,
    hashIntergrity,
    blockNum,
    timestamp,
    blockSignatures,
    smartSourceSignatures,
    balances,
    fullValidation
  };

  enum SeverityLevel : uint8_t {
    WarningsAsErrors = 1,
    GreaterThanWarnings,
    OnlyFatalErrors
  };

  explicit BlockValidator(const BlockChain&);
  bool validateBlock(const csdb::Pool&, ValidationLevel = hashIntergrity,
                     SeverityLevel = GreaterThanWarnings);

  BlockValidator(const BlockValidator&) = delete;
  BlockValidator(BlockValidator&&) = delete;
  BlockValidator& operator=(const BlockValidator&) = delete;
  BlockValidator& operator=(BlockValidator&&) = delete;

private:
  enum ErrorType : uint8_t {
    noError = 0,
    warning = 1 << 1,
    error = 1 << 2,
    fatalError = 1 << 3
  };

  ErrorType checkHashIntergrity(const csdb::Pool&);
  ErrorType checkBlockNum(const csdb::Pool&) { return noError; }
  ErrorType checkTimestamp(const csdb::Pool&) { return noError; }
  ErrorType checkBlockSignatures(const csdb::Pool&) { return noError; }
  ErrorType checkSmartSourceSignatures(const csdb::Pool&) { return noError; }
  ErrorType checkBalances(const csdb::Pool&) { return noError; }
  ErrorType checkAllTransactions(const csdb::Pool&) { return noError; }

  bool return_(ErrorType, SeverityLevel);

  const BlockChain& bc_;
  std::unique_ptr<Fee> feeCounter_;   
  std::unique_ptr<WalletsState> wallets_;
  std::unique_ptr<IterValidator> iterValidator_;
};
} // namespace cs
#endif // BLOCKVALIDATOR_HPP
