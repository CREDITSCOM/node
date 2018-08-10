#include <limits>

#include <lib/system/logger.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>

#include "wallets_cache.hpp"
#include "blockchain.hpp"

#include "sys/timeb.h"

#define TRACE()
using namespace Credits;

BlockChain::BlockChain(const char* path)
{
	std::cerr << "Trying to open DB..." << std::endl;
    if (!storage_.open(path))
    {
        LOG_ERROR("Couldn't open database at " << path);
        return;
    }

    if (!loadCache())
        return;

    good_ = true;
}

bool BlockChain::loadCache()
{
    try
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        walletsCache_.reset(new WalletsCache(WalletsCache::Config()));
        walletsCache_->load(*this);
    }
    catch (std::exception& e)
    {
        LOG_ERROR("Exc=" << e.what());
        return false;
    }
    catch (...)
    {
        LOG_ERROR("Exc=...");
        return false;
    }
    return true;
}

bool BlockChain::updateCache(csdb::Pool& pool)
{
    try
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        walletsCache_->updateFrom(pool);
    }
    catch (std::exception& e)
    {
        LOG_ERROR("Exc=" << e.what());
        return false;
    }
    catch (...)
    {
        LOG_ERROR("Exc=...");
        return false;
    }
    return true;
}

void BlockChain::writeLastBlock(csdb::Pool& pool) {
	std::lock_guard<std::mutex> l(dbLock_);

    pool.set_storage(storage_);
    pool.set_previous_hash(storage_.last_hash());
    pool.set_sequence(storage_.size());

    if (!pool.compose()) {
        LOG_ERROR("Couldn't compose block");
        return;
    }

    if (!pool.save()) {
        LOG_ERROR("Couldn't save block");
        return;
    }

    new_block_cv.notify_all();

    if (!updateCache(pool)) {
      LOG_ERROR("Couldn't update cache");
    }
}

csdb::PoolHash BlockChain::getLastHash() const {
	std::lock_guard<std::mutex> l(dbLock_);
	return storage_.last_hash();
}

size_t BlockChain::getSize() const {
	std::lock_guard<std::mutex> l(dbLock_);
	return storage_.size();
}

csdb::Pool BlockChain::loadBlock(const csdb::PoolHash& ph) const {
	std::lock_guard<std::mutex> l(dbLock_);
	auto pool = storage_.pool_load(ph);
	return pool;
}

csdb::Pool BlockChain::loadBlockMeta(const csdb::PoolHash& ph, size_t& cnt) const {
	std::lock_guard<std::mutex> l(dbLock_);
	return storage_.pool_load_meta(ph, cnt);
}

csdb::Transaction BlockChain::loadTransaction(const csdb::TransactionID& transId) const {
	std::lock_guard<std::mutex> l(dbLock_);
	return storage_.transaction(transId);
}

csdb::Address
BlockChain::getAddressFromKey(const std::string &key)
{
    TRACE();
    std::string pk(static_cast<size_t>(PUBLIC_KEY_LENGTH), '\0');
    TRACE();
    std::copy(key.rbegin(), std::min(key.rbegin() + PUBLIC_KEY_LENGTH, key.rend()), pk.rbegin());
    TRACE();
    csdb::Address res = csdb::Address::from_public_key(pk.data());
    TRACE();
    return res;
}

csdb::Amount BlockChain::getBalance(const csdb::Address& address) const {
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        const WalletsCache::WalletData* walData = walletsCache_->findWallet(address.public_key());
        if (walData)
            return walData->balance_;
    }
    return calcBalance(address);
}

csdb::Amount BlockChain::calcBalance(csdb::Address address) const {
	csdb::Amount result(0);

    csdb::Pool curr = loadBlock(getLastHash());
    while (curr.is_valid()) {
		for (size_t i = 0; i < curr.transactions_count(); i++) {
			csdb::Transaction tr = curr.transaction(i);
			if (tr.source() == address)
                result -= tr.amount();
			else if (tr.target() == address)
				result += tr.amount();
		}
		curr = loadBlock(curr.previous_hash());
	}
	return result;
}

void
BlockChain::wait_for_block()
{
  std::unique_lock<std::mutex> l(dbLock_);
  auto ls = storage_.size();
  new_block_cv.wait(l, [ls, this] { return storage_.size() != ls; });
}

namespace
{
    class TrxLoader
    {
    public:
        using Transaction = std::vector<csdb::Transaction>;
    public:
        TrxLoader(csdb::Address addr, const BlockChain& blockchain, Transaction& transactions)
            : addr_(addr), blockchain_(blockchain), transactions_(transactions)
        {}

        bool load(const csdb::PoolHash& poolHash, int64_t& offset, int64_t limit, csdb::PoolHash& prevPoolHash)
        {
            csdb::Pool curr = blockchain_.loadBlock(poolHash);
            if (!curr.is_valid())
                return false;

            if (curr.transactions_count())
            {
                auto curIdx = static_cast<csdb::TransactionID::sequence_t>(curr.transactions_count() - 1);

                while (true)
                {
                    auto trans = curr.transaction(curIdx);
                    //std::cerr << "Ladder: " << trans.target().to_string() << " <- "
                    //          << trans.source().to_string() << " of "
                    //          << trans.amount().integral() << std::endl;
                    if (trans.target() == addr_ || trans.source() == addr_)
                    {
                        if (offset == 0)
                            transactions_.push_back(trans);
                        else
                            --offset;
                    }

                    if (transactions_.size() == limit)
                        break;

                    if (curIdx == 0)
                        break;
                    --curIdx;
                }
            }

            prevPoolHash = curr.previous_hash();
            return true;
        }

    private:
        csdb::Address addr_;
        const BlockChain& blockchain_;
        Transaction & transactions_;
    };
}

void BlockChain::getTransactions(Transactions& transactions, csdb::Address address, int64_t offset, const int64_t limit) const
{
    TrxLoader trxLoader(address, *this, transactions);

    WalletsCache::WalletData::PoolsHashes hashesArray;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        const WalletsCache::WalletData* walData = walletsCache_->findWallet(address.public_key());
        if (walData)
        {
            hashesArray = walData->poolsHashes_;
        }
    }

    csdb::PoolHash prevHash = getLastHash();
    if (!hashesArray.empty())
    {
        for (size_t i = hashesArray.size() - 1; i != std::numeric_limits<decltype(i)>::max(); --i)
        {
            const auto& poolHashData = hashesArray[i];

            if (poolHashData.trxNum < WalletsCache::WalletData::PoolHashData::maxTrxNum  &&
                poolHashData.trxNum <= offset)
            {
                offset -= poolHashData.trxNum;
                continue;
            }

            csdb::PoolHash currHash;
            WalletsCache::convert(poolHashData.poolHash, currHash);

            if (!trxLoader.load(currHash, offset, limit, prevHash))
                return;
            if (transactions.size() >= size_t(limit))
                return;
        }
    }

    while (true)
    {
        csdb::PoolHash currHash = prevHash;
        if (!trxLoader.load(currHash, offset, limit, prevHash))
            break;
    }
}
