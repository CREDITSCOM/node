#ifndef BLOCK_VALIDATOR_HPP
#define BLOCK_VALIDATOR_HPP

#include <map>
#include <memory>

#include <csdb/pool.hpp>

class Node;
class BlockChain;

namespace cs {

class ValidationPlugin;
class WalletsState;

class BlockValidator {
public:
    using ValidationFlags = uint32_t;

    enum ValidationLevel : uint32_t {
        noValidation = 0,
        hashIntergrity = 1,
        blockNum = 1 << 1,
        timestamp = 1 << 2,
        blockSignatures = 1 << 3,
        smartSignatures = 1 << 4,
        balances = 1 << 5,
        transactionsSignatures = 1 << 6,
        smartStates = 1 << 7
    };

    enum SeverityLevel : uint8_t {
        warningsAsErrors = 1,
        greaterThanWarnings,
        onlyFatalErrors
    };

    explicit BlockValidator(Node&);
    ~BlockValidator();
    bool validateBlock(const csdb::Pool&, ValidationFlags = hashIntergrity, SeverityLevel = greaterThanWarnings);

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

    Node& node_;
    const BlockChain& bc_;

    std::map<ValidationLevel, std::unique_ptr<ValidationPlugin>> plugins_;

    friend class ValidationPlugin;

    std::shared_ptr<WalletsState> wallets_;
    csdb::Pool prevBlock_;
};
}  // namespace cs
#endif  // BLOCKVALIDATOR_HPP
