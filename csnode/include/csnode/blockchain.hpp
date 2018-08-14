#pragma once

#include <list>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

#include <fstream>
#include <iostream>
#include <string>

#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/pool.h>
#include <csdb/storage.h>

#include <csnode/wallets_cache.hpp>

#include <condition_variable>
#include <mutex>

class BlockChain {

  struct Headtag
  {
    uint32_t head;
    uint32_t tag;
  };

public:

    std::string dbs_fname;
    using Transactions = std::vector<csdb::Transaction>;

	  BlockChain(const char* path);

    void putBlock(csdb::Pool& pool);
    
    void writeLastBlock(csdb::Pool& pool);

	  csdb::PoolHash getLastHash() const;
	  size_t getSize() const;

	  csdb::Pool loadBlock(const csdb::PoolHash&) const;
	  csdb::Pool loadBlockMeta(const csdb::PoolHash&, size_t& cnt) const;
	  csdb::Transaction loadTransaction(const csdb::TransactionID&) const;

	  csdb::Amount getBalance(const csdb::Address&) const;

    void getTransactions(Transactions& transactions, csdb::Address address, int64_t offset, const int64_t limit) const;

    bool isGood() const { return good_; }

    void wait_for_block();

    static csdb::Address getAddressFromKey(const std::string &);

    void setLastWrittenSequence(uint32_t seq);
    uint32_t getLastWrittenSequence();

    uint32_t getRequestedBlockNumber();

    void setGlobalSequence(uint32_t seq);
    uint32_t getGlobalSequence();

    bool getBlockRequestNeed();

    csdb::PoolHash getHashBySequence(uint32_t seq);

private:
    Headtag ht;
    bool loadCache();
    bool updateCache(csdb::Pool& pool);
    csdb::Amount calcBalance(csdb::Address) const;
    void writeBlock(csdb::Pool& pool);
    void BlockChain::onBlockReceived(csdb::Pool& pool);
    void writeGenesisBlock();

private:
	bool good_ = false;

	mutable std::mutex dbLock_;
	csdb::Storage storage_;

  csdb::PoolHash lastHash_;
  uint32_t last_written_sequence;
  uint32_t global_sequence;
  bool blockRequestIsNeeded;

  std::vector <csdb::PoolHash> blockHashes_;

  std::unique_ptr<Credits::WalletsCache> walletsCache_;
  mutable std::mutex cacheMutex_;

  std::condition_variable new_block_cv;
};
