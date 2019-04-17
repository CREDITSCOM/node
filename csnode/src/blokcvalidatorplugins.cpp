#include <csnode/blockvalidatorplugins.hpp>

#include <string>

#include <csdb/pool.hpp>
#include <csnode/blockchain.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/common.hpp>
#include <csnode/itervalidator.hpp>
#include <csnode/fee.hpp>
#include <csnode/walletsstate.hpp>
#include <csdb/pool.hpp>

namespace {
const char* log_prefix = "BlockValidator: ";
const cs::Sequence kGapBtwNeighbourBlocks = 1;
const csdb::user_field_id_t kTimeStampUserFieldNum = 0;
} // namespace

namespace cs {

ValidationPlugin::ErrorType HashValidator::validateBlock(const csdb::Pool& block) {
  ErrorType res = ErrorType::noError;
  auto prevHash = block.previous_hash();
  auto& prevBlock = getPrevBlock();
  auto data = prevBlock.to_binary();
  auto countedPrevHash = csdb::PoolHash::calc_from_data(cs::Bytes(data.data(),
                                                          data.data() +
                                                          prevBlock.hashingLength()));
  if (prevHash != countedPrevHash) {
    csfatal() << log_prefix << ": prev pool's (" << prevBlock.sequence()
              << ") hash != real prev pool's hash";
    res = ErrorType::fatalError;      
  }
  return res;
}

ValidationPlugin::ErrorType BlockNumValidator::validateBlock(const csdb::Pool& block) {
  ErrorType res = ErrorType::noError;
  auto& prevBlock = getPrevBlock();
  if (block.sequence() - prevBlock.sequence() != kGapBtwNeighbourBlocks) {
    cserror() << log_prefix << "Current block's sequence is " << block.sequence()
              << ", previous block sequence is " << prevBlock.sequence();
    res = ErrorType::error;
  }
  return res;
}

ValidationPlugin::ErrorType TimestampValidator::validateBlock(const csdb::Pool& block) {
  ErrorType res = ErrorType::noError;
  auto& prevBlock = getPrevBlock();

  auto prevBlockTimestampUf = prevBlock.user_field(kTimeStampUserFieldNum);
  if (!prevBlockTimestampUf.is_valid()) {
    cswarning() << log_prefix << "Block with sequence " << prevBlock.sequence() << " has no timestamp";
    return ErrorType::warning;
  }
  auto currentBlockTimestampUf = block.user_field(kTimeStampUserFieldNum);
  if (!currentBlockTimestampUf.is_valid()) {
    cswarning() << log_prefix << "Block with sequence " << block.sequence() << " has no timestamp";
    return ErrorType::warning;
  }

  auto prevBlockTimestamp = std::stoll(prevBlockTimestampUf.value<std::string>());
  auto currentBlockTimestamp = std::stoll(currentBlockTimestampUf.value<std::string>());
  if (currentBlockTimestamp < prevBlockTimestamp) {
    cswarning() << log_prefix << "Block with sequence " << block.sequence()
                << " has timestamp " << currentBlockTimestamp
                << " less than " << prevBlockTimestamp
                << " in block with sequence " << prevBlock.sequence();
    res = ErrorType::warning;
  }
  return res;
}

ValidationPlugin::ErrorType BlockSignaturesValidator::validateBlock(const csdb::Pool&) {
  return ErrorType::noError;
}

ValidationPlugin::ErrorType SmartSourceSignaturesValidator::validateBlock(const csdb::Pool&) {
  return ErrorType::noError;
}

ValidationPlugin::ErrorType BalanceChecker::validateBlock(const csdb::Pool&) {
  return ErrorType::noError;
}

ValidationPlugin::ErrorType TransactionsChecker::validateBlock(const csdb::Pool&) {
  return ErrorType::noError;
}

} // namespace cs
