#include <csnode/blockvalidator.hpp>

#include <csdb/pool.hpp>
#include <csnode/blockchain.hpp>
#include <lib/system/logger.hpp>

namespace {
const char* log_prefix = "BlockValidator: ";
} // namespace

namespace cs {

BlockValidator::BlockValidator(const BlockChain& bc)
    : bc_(bc),
      feeCounter_(::std::make_unique<Fee>()),
      wallets_(::std::make_unique<WalletsState>(bc_)),
      iterValidator_(::std::make_unique<IterValidator>(*wallets_.get())) {}

inline bool BlockValidator::return_(ErrorType error, SeverityLevel severity) {
  return !(error >> severity);
}

bool BlockValidator::validateBlock(const csdb::Pool& block, ValidationLevel level,
                                   SeverityLevel severity) {
  if (level == ValidationLevel::noValidation) {
    return true;
  }

  ErrorType validationResult = checkHashIntergrity(block);
  if (level == ValidationLevel::hashIntergrity || !return_(validationResult, severity)) {
    return return_(validationResult, severity);
  }

  validationResult = checkBlockNum(block);
  if (level == ValidationLevel::blockNum || !return_(validationResult, severity)) {
    return return_(validationResult, severity);
  }

  validationResult = checkTimestamp(block);
  if (level == ValidationLevel::timestamp || !return_(validationResult, severity)) {
    return return_(validationResult, severity);
  }

  validationResult = checkBlockSignatures(block);
  if (level == ValidationLevel::blockSignatures || !return_(validationResult, severity)) {
    return return_(validationResult, severity);
  }

  validationResult = checkSmartSourceSignatures(block);
  if (level == ValidationLevel::smartSourceSignatures || !return_(validationResult, severity)) {
    return return_(validationResult, severity);
  }

  validationResult = checkBalances(block);
  if (level == ValidationLevel::balances || !return_(validationResult, severity)) {
    return return_(validationResult, severity);
  }

  return return_(checkAllTransactions(block), severity);
}

BlockValidator::ErrorType BlockValidator::checkHashIntergrity(const csdb::Pool& block) {
  ErrorType res = noError;
  if (block.sequence() == 0) {
    return res;
  }

  auto prev_hash = block.previous_hash();
  auto prev_block = bc_.loadBlock(prev_hash);
  auto data = prev_block.to_binary();
  auto counted_prev_hash = csdb::PoolHash::calc_from_data(cs::Bytes(data.data(),
                                                          data.data() +
                                                          prev_block.hashingLength()));

  if (prev_hash != counted_prev_hash) {
    cserror() << log_prefix << ": prev pool's (" << prev_block.sequence()
              << ") hash != real prev pool's hash";
    res = fatalError;      
  }

  return res;
}



} // namespace cs
