#ifndef BLOCK_VALIDATOR_PLUGINS_HPP
#define BLOCK_VALIDATOR_PLUGINS_HPP

#include <csnode/blockvalidator.hpp>

namespace cs {

class ValidationPlugin {
public:
  ValidationPlugin(BlockValidator& bv) : block_validator_(bv) {}

  using ErrorType = BlockValidator::ErrorType;
  virtual ErrorType validateBlock(const csdb::Pool&) = 0;

protected:
  const BlockChain& getBlockChain() { return block_validator_.bc_; }
  auto getFeeCounter() { return block_validator_.feeCounter_; }
  auto getWallets() { return block_validator_.wallets_; }
  auto getIterValidator() { return block_validator_.iterValidator_; }
  auto& getPrevBlock() { return block_validator_.prev_block_; }

private:
  BlockValidator& block_validator_;
};

class HashValidator : public ValidationPlugin {
public:
  HashValidator(BlockValidator& bv) : ValidationPlugin(bv) {}
  ErrorType validateBlock(const csdb::Pool&) override;
};

class BlockNumValidator : public ValidationPlugin {
public:
  BlockNumValidator(BlockValidator& bv) : ValidationPlugin(bv) {}
  ErrorType validateBlock(const csdb::Pool&) override;
};

class TimestampValidator : public ValidationPlugin {
public:
  TimestampValidator(BlockValidator& bv) : ValidationPlugin(bv) {}
  ErrorType validateBlock(const csdb::Pool&) override;
};

class BlockSignaturesValidator : public ValidationPlugin {
public:
  BlockSignaturesValidator(BlockValidator& bv) : ValidationPlugin(bv) {}
  ErrorType validateBlock(const csdb::Pool&) override;
};

class SmartSourceSignaturesValidator : public ValidationPlugin {
public:
  SmartSourceSignaturesValidator(BlockValidator& bv) : ValidationPlugin(bv) {}
  ErrorType validateBlock(const csdb::Pool&) override;
};

class BalanceChecker : public ValidationPlugin {
public:
  BalanceChecker(BlockValidator& bv) : ValidationPlugin(bv) {}
  ErrorType validateBlock(const csdb::Pool&) override;
};

class TransactionsChecker : public ValidationPlugin {
public:
  TransactionsChecker(BlockValidator& bv) : ValidationPlugin(bv) {}
  ErrorType validateBlock(const csdb::Pool&) override;
};
} // namespace cs
#endif // BLOCK_VALIDATOR_PLUGINS_HPP
