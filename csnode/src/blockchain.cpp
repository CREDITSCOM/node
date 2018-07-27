#include <lib/system/logger.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>

#include "blockchain.hpp"
#include "sys/timeb.h"

BlockChain::BlockChain(const char* path) {
  std::cerr << "Trying to open DB..." << std::endl;
  if (storage_.open(path))
    good_ = true;
  else
    LOG_ERROR("Couldn't open database at " << path);
}

void BlockChain::writeLastBlock(csdb::Pool& pool) {
  std::lock_guard<std::mutex> l(dbLock_);

  pool.set_storage(storage_);
  pool.set_previous_hash(storage_.last_hash());
  pool.set_sequence(storage_.size());

  if (!pool.compose())
    LOG_ERROR("Couldn't compose block");

  if (!pool.save())
    LOG_ERROR("Couldn't save block");
}

csdb::PoolHash BlockChain::getLastHash() {
  std::lock_guard<std::mutex> l(dbLock_);
  return storage_.last_hash();
}

size_t BlockChain::getSize() {
  std::lock_guard<std::mutex> l(dbLock_);
  return storage_.size();
}

csdb::Pool BlockChain::loadBlock(const csdb::PoolHash& ph) {
  std::lock_guard<std::mutex> l(dbLock_);
  auto pool = storage_.pool_load(ph);
  return pool;
}

csdb::Pool BlockChain::loadBlockMeta(const csdb::PoolHash& ph, size_t& cnt) {
  std::lock_guard<std::mutex> l(dbLock_);
  return storage_.pool_load(ph);
}

csdb::Transaction BlockChain::loadTransaction(const csdb::TransactionID& transId) {
  std::lock_guard<std::mutex> l(dbLock_);
  return storage_.transaction(transId);
}

csdb::Address BlockChain::getAddressFromKey(const char* key) {
  PublicKey hashedKey = getHashedPublicKey(key);
  csdb::internal::byte_array barr(hashedKey.str, hashedKey.str + HASH_LENGTH);
  return csdb::Address::from_public_key(barr);
}

csdb::Amount BlockChain::getBalance(const csdb::Address& address) {
  csdb::PoolHash lastCheckedHash;
  csdb::Amount cachedBalance(0);

  csdb::PoolHash lastHash = getLastHash();

  auto cacheIter = balancesCache_.find(address);
  if (cacheIter != balancesCache_.end()) {
    lastCheckedHash = cacheIter->second.first;
    cachedBalance = cacheIter->second.second;

    if (lastCheckedHash == lastHash) return cachedBalance;
  }

  csdb::Amount result(0);
  bool foundSource = false;
  csdb::Pool curr = loadBlock(lastHash);

  while (curr.is_valid() && curr.hash() != lastCheckedHash) {
    csdb::Amount recordedBalance;

    for (size_t i = 0; i < curr.transactions_count(); i++) {
      csdb::Transaction tr = curr.transaction(i);
      if (tr.source() == address) {
        foundSource = true;
        recordedBalance = tr.balance();
      }
      else if (tr.target() == address)
        result += tr.amount();
    }

    if (foundSource) {
      result += recordedBalance;
      break;
    }

    curr = loadBlock(curr.previous_hash());
  }

  if (!foundSource) result += cachedBalance;
  balancesCache_[address] = std::make_pair(lastHash, result);

  return result;
}
