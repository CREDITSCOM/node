#ifndef BLOCK_VALIDATOR_PLUGINS_HPP
#define BLOCK_VALIDATOR_PLUGINS_HPP

#include <vector>

#include <cscrypto/cryptotypes.hpp>
#include <csdb/amount.hpp>
#include <csdb/transaction.hpp>
#include <csnode/blockvalidator.hpp>
#include <csnode/transactionspacket.hpp>
#include <lib/system/common.hpp>

namespace cs {

class ValidationPlugin {
public:
    ValidationPlugin(BlockValidator& bv)
    : blockValidator_(bv) {
    }

    virtual ~ValidationPlugin() = default;

    using ErrorType = BlockValidator::ErrorType;
    virtual ErrorType validateBlock(const csdb::Pool&) = 0;

protected:
    const BlockChain& getBlockChain() {
        return blockValidator_.bc_;
    }
    auto getWallets() {
        return blockValidator_.wallets_;
    }
    auto& getPrevBlock() {
        return blockValidator_.prevBlock_;
    }

private:
    BlockValidator& blockValidator_;
};

class HashValidator : public ValidationPlugin {
public:
    HashValidator(BlockValidator& bv)
    : ValidationPlugin(bv) {
    }
    ErrorType validateBlock(const csdb::Pool&) override;
};

class BlockNumValidator : public ValidationPlugin {
public:
    BlockNumValidator(BlockValidator& bv)
    : ValidationPlugin(bv) {
    }
    ErrorType validateBlock(const csdb::Pool&) override;
};

class TimestampValidator : public ValidationPlugin {
public:
    TimestampValidator(BlockValidator& bv)
    : ValidationPlugin(bv) {
    }
    ErrorType validateBlock(const csdb::Pool&) override;
};

class BlockSignaturesValidator : public ValidationPlugin {
public:
    BlockSignaturesValidator(BlockValidator& bv)
    : ValidationPlugin(bv) {
    }
    ErrorType validateBlock(const csdb::Pool&) override;
};

class SmartSourceSignaturesValidator : public ValidationPlugin {
public:
    using Transactions = std::vector<csdb::Transaction>;
    using SmartSignatures = std::vector<csdb::Pool::SmartSignature>;
    using Packets = std::vector<cs::TransactionsPacket>;

    SmartSourceSignaturesValidator(BlockValidator& bv)
    : ValidationPlugin(bv) {
    }
    ErrorType validateBlock(const csdb::Pool&) override;

private:
    bool containsNewState(const Transactions&);
    Packets grepNewStatesPacks(const Transactions&, bool switchFees);
    bool checkSignatures(const SmartSignatures&, const Packets&);

    // must be performed if block version is 0
    // to pass validation
    csdb::Transaction switchCountedFee(const csdb::Transaction& newState);
};

///
/// @brief check balances when prev block was added to blockchain
///
class BalanceChecker : public ValidationPlugin {
public:
    BalanceChecker(BlockValidator& bv)
    : ValidationPlugin(bv) {
    }
    ErrorType validateBlock(const csdb::Pool&) override;

private:
    static constexpr csdb::Amount zeroBalance_ = 0;
};

class TransactionsChecker : public ValidationPlugin {
public:
    TransactionsChecker(BlockValidator& bv)
    : ValidationPlugin(bv) {
    }
    ErrorType validateBlock(const csdb::Pool&) override;

private:
    bool checkSignature(const csdb::Transaction&);
};
}  // namespace cs
#endif  // BLOCK_VALIDATOR_PLUGINS_HPP
