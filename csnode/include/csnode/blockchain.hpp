#ifndef __BLOCKCHAIN_HPP__
#define __BLOCKCHAIN_HPP__
#include <map>
#include <mutex>

#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/pool.h>
#include <csdb/storage.h>

class BlockChain {
public:
  BlockChain(const char* path);

  void writeLastBlock(csdb::Pool& pool);

  csdb::PoolHash getLastHash();
  size_t getSize();

  csdb::Pool loadBlock(const csdb::PoolHash&);
  csdb::Pool loadBlockMeta(const csdb::PoolHash&, size_t& cnt);
  csdb::Transaction loadTransaction(const csdb::TransactionID&);

  csdb::Amount getBalance(const csdb::Address&);

  bool isGood() const { return good_; }

  static csdb::Address getAddressFromKey(const char*);

private:
  bool good_ = false;

  std::map<csdb::Address, std::pair<csdb::PoolHash, csdb::Amount>> balancesCache_;

  std::mutex dbLock_;
  csdb::Storage storage_;
};

#endif // __BLOCKCHAIN_HPP__
