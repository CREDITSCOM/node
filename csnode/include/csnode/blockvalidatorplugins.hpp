#ifndef BLOCK_VALIDATOR_PLUGINS_HPP
#define BLOCK_VALIDATOR_PLUGINS_HPP

#include <csnode/blockvalidator.hpp>

namespace cs {

class ValidationPlugin {
public:
  ValidationPlugin(BlockValidator& bv) : blockValidator_(bv) {}

  using ErrorType = BlockValidator::ErrorType;
  virtual ErrorType validateBlock(const csdb::Pool&) = 0;

protected:
  const BlockChain& getBlockChain() { return blockValidator_.bc_; }
  auto getFeeCounter() { return blockValidator_.feeCounter_; }
  auto getWallets() { return blockValidator_.wallets_; }
  auto getIterValidator() { return blockValidator_.iterValidator_; }
  auto& getPrevBlock() { return blockValidator_.prevBlock_; }

private:
  BlockValidator& blockValidator_;
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
