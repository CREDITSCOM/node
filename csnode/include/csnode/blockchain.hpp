#pragma once

#include <map>
#include <memory>
#include <mutex>

#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/pool.h>
#include <csdb/storage.h>

#include <csnode/wallets_cache.hpp>

#include <condition_variable>
#include <mutex>

class BlockChain {
public:
    using Transactions = std::vector<csdb::Transaction>;

	BlockChain(const char* path);

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

private:
    bool loadCache();
    bool updateCache(csdb::Pool& pool);
    csdb::Amount calcBalance(csdb::Address) const;

private:
	bool good_ = false;

	mutable std::mutex dbLock_;
	csdb::Storage storage_;

    std::unique_ptr<Credits::WalletsCache> walletsCache_;
    mutable std::mutex cacheMutex_;

    std::condition_variable new_block_cv;
};
