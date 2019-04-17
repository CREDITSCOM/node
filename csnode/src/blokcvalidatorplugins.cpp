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
#include <cscrypto/cscrypto.hpp>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace {
const char* log_prefix = "BlockValidator: ";
const cs::Sequence kGapBtwNeighbourBlocks = 1;
const csdb::user_field_id_t kTimeStampUserFieldNum = 0;
} // namespace

namespace cs {

ValidationPlugin::ErrorType HashValidator::validateBlock(const csdb::Pool& block) {
  auto prevHash = block.previous_hash();
  auto& prevBlock = getPrevBlock();
  auto data = prevBlock.to_binary();
  auto countedPrevHash = csdb::PoolHash::calc_from_data(cs::Bytes(data.data(),
                                                          data.data() +
                                                          prevBlock.hashingLength()));
  if (prevHash != countedPrevHash) {
    csfatal() << log_prefix << ": prev pool's (" << prevBlock.sequence()
              << ") hash != real prev pool's hash";
    return ErrorType::fatalError;      
  }
  return ErrorType::noError;
}

ValidationPlugin::ErrorType BlockNumValidator::validateBlock(const csdb::Pool& block) {
  auto& prevBlock = getPrevBlock();
  if (block.sequence() - prevBlock.sequence() != kGapBtwNeighbourBlocks) {
    cserror() << log_prefix << "Current block's sequence is " << block.sequence()
              << ", previous block sequence is " << prevBlock.sequence();
    return ErrorType::error;
  }
  return ErrorType::noError;
}

ValidationPlugin::ErrorType TimestampValidator::validateBlock(const csdb::Pool& block) {
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
    return ErrorType::warning;
  }
  return ErrorType::noError;
}

ValidationPlugin::ErrorType BlockSignaturesValidator::validateBlock(const csdb::Pool& block) {
  uint64_t realTrustedMask = block.realTrusted();
#ifdef _MSC_VER
  size_t numOfRealTrusted = static_cast<decltype(numOfRealTrusted)>(__popcnt64(realTrustedMask));
#else
  size_t numOfRealTrusted = static_cast<decltype(numOfRealTrusted)>(__builtin_popcountl(realTrustedMask));
#endif

  auto signatures = block.signatures();
  if (signatures.size() != numOfRealTrusted) {
    cserror() << log_prefix << "in block " << block.sequence()
              << " num of signatures (" << signatures.size()
              << ") != num of real trusted (" << numOfRealTrusted << ")";
    return ErrorType::error;
  }

  auto confidants = block.confidants();
  const size_t maxTrustedNum = sizeof(realTrustedMask) * 8;
  if (confidants.size() > maxTrustedNum) {
    cserror() << log_prefix << "in block " << block.sequence()
              << " num of confidants " << confidants.size()
              << " is greated than max bits in realTrustedMask";
    return ErrorType::error;
  }

  size_t checkingSignature = 0;
  auto signedData = cscrypto::calculateHash(block.to_binary().data(), block.hashingLength());
  for (size_t i = 0; i < confidants.size(); ++i) {
    if (realTrustedMask & (1 << i)) {
      if (!cscrypto::verifySignature(signatures[checkingSignature],
                                     confidants[i],
                                     signedData.data(),
                                     cscrypto::kHashSize)) {
        cserror() << log_prefix << "block " << block.sequence()
                  << "has invalid signatures";
        return ErrorType::error;
      }
      ++checkingSignature;
    }
  }

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
