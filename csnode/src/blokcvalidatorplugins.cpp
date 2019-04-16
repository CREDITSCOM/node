#include <csnode/blockvalidatorplugins.hpp>

#include <csdb/pool.hpp>
#include <csnode/blockchain.hpp>
#include <lib/system/logger.hpp>
#include <csnode/itervalidator.hpp>
#include <csnode/fee.hpp>
#include <csnode/walletsstate.hpp>
#include <csdb/pool.hpp>

namespace {
const char* log_prefix = "BlockValidator: ";
} // namespace

namespace cs {

ValidationPlugin::ErrorType HashValidator::validateBlock(const csdb::Pool& block) {
  ErrorType res = ErrorType::noError;
  auto prev_hash = block.previous_hash();
  auto& prev_block = getPrevBlock();
  auto data = prev_block.to_binary();
  auto counted_prev_hash = csdb::PoolHash::calc_from_data(cs::Bytes(data.data(),
                                                          data.data() +
                                                          prev_block.hashingLength()));

  if (prev_hash != counted_prev_hash) {
    cserror() << log_prefix << ": prev pool's (" << prev_block.sequence()
              << ") hash != real prev pool's hash";
    res = ErrorType::fatalError;      
  }

  return res;
}

ValidationPlugin::ErrorType BlockNumValidator::validateBlock(const csdb::Pool&) {
  return ErrorType::noError;
}

ValidationPlugin::ErrorType TimestampValidator::validateBlock(const csdb::Pool&) {
  return ErrorType::noError;
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
