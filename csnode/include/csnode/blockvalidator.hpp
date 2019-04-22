#ifndef BLOCK_VALIDATOR_HPP
#define BLOCK_VALIDATOR_HPP

#include <memory>
#include <vector>

#include <csdb/pool.hpp>

class BlockChain;

namespace cs {

class ValidationPlugin;
class Fee;
class IterValidator;
class WalletsState; 

class BlockValidator {
public:
  enum ValidationLevel : uint8_t {
    hashIntergrity = 0,
    blockNum,
    timestamp,
    blockSignatures,
    smartSourceSignatures,
    balances,
    fullValidation,
    noValidation = 255
  };

  enum SeverityLevel : uint8_t {
    warningsAsErrors = 1,
    greaterThanWarnings,
    onlyFatalErrors
  };

  explicit BlockValidator(const BlockChain&);
  ~BlockValidator();
  bool validateBlock(const csdb::Pool&, ValidationLevel = hashIntergrity,
                     SeverityLevel = greaterThanWarnings);

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

  bool return_(ErrorType, SeverityLevel);

  const BlockChain& bc_;
  std::vector<std::unique_ptr<ValidationPlugin>> plugins_;

  friend class ValidationPlugin;

  std::shared_ptr<WalletsState> wallets_;
  csdb::Pool prevBlock_;
};
} // namespace cs
#endif // BLOCKVALIDATOR_HPP
