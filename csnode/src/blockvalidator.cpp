#include <csnode/blockvalidator.hpp>

#include <csnode/blockchain.hpp>
#include <csnode/itervalidator.hpp>
#include <csnode/fee.hpp>
#include <csnode/walletsstate.hpp>

#include <csnode/blockvalidatorplugins.hpp>

namespace cs {

BlockValidator::BlockValidator(const BlockChain& bc)
    : bc_(bc),
      feeCounter_(::std::make_shared<Fee>()),
      wallets_(::std::make_shared<WalletsState>(bc_)),
      iterValidator_(::std::make_shared<IterValidator>(*wallets_.get())) {
  plugins_.push_back(std::make_unique<HashValidator>(*this));
  plugins_.push_back(std::make_unique<BlockNumValidator>(*this));
  plugins_.push_back(std::make_unique<TimestampValidator>(*this));
  plugins_.push_back(std::make_unique<BlockSignaturesValidator>(*this));
  plugins_.push_back(std::make_unique<SmartSourceSignaturesValidator>(*this));
  plugins_.push_back(std::make_unique<BalanceChecker>(*this));
  plugins_.push_back(std::make_unique<TransactionsChecker>(*this));
}

BlockValidator::~BlockValidator() {}

inline bool BlockValidator::return_(ErrorType error, SeverityLevel severity) {
  return !(error >> severity);
}

bool BlockValidator::validateBlock(const csdb::Pool& block, ValidationLevel level,
                                   SeverityLevel severity) {
  if (level == ValidationLevel::noValidation || block.sequence() == 0) {
    return true;
  }

  if (!block.is_valid()) {
    cserror() << "BlockValidator: invalid block received";
    return false;
  }

  if (!prevBlock_.is_valid() || block.sequence() - prevBlock_.sequence() != 1) {
    prevBlock_ = bc_.loadBlock(block.previous_hash());
    if (!prevBlock_.is_valid()) {
      cserror() << "BlockValidator: block with hash "
                << block.previous_hash().to_string() << " is not valid.";
      return false;
    }
  }
 
  ErrorType validationResult = noError;
  for (uint8_t i = 0; i <= static_cast<uint8_t>(level); ++i) {
    validationResult = plugins_[i]->validateBlock(block);
    if (!return_(validationResult, severity)) {
      return false;
    }
  }

  prevBlock_ = block;
  return true;
}
} // namespace cs
