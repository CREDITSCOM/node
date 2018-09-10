#include <algorithm>
#include <lib/system/logger.hpp>

#include "blockchain.hpp"
#include "wallets_cache.hpp"

using namespace std;

namespace Credits
{
void WalletsCache::convert(const csdb::PoolHash& poolHashDb, PoolHash& poolHashCache)
{
    const csdb::internal::byte_array& phVec = poolHashDb.to_binary();
    copy(phVec.begin(), phVec.end(), poolHashCache.begin());
}

void WalletsCache::convert(const PoolHash& poolHashCache, csdb::PoolHash& poolHashDb)
{
    csdb::internal::byte_array hashBytes(poolHashCache.begin(), poolHashCache.end());
    poolHashDb = csdb::PoolHash::from_binary(hashBytes);
}

WalletsCache::WalletsCache(const Config& config)
    : config_(config)
    , maxWalletsNum_(config_.memSize >> 12)
{
    /*LOG_NOTICE(__FUNCTION__ << ": sizeof(PoolHash)=" << sizeof(PoolHash) << " alignof(PoolHash)=" << alignof(PoolHash));
    LOG_NOTICE(__FUNCTION__ << ": sizeof(WalletData::PoolHashData)=" << sizeof(WalletData::PoolHashData) << " alignof(WalletData::PoolHashData)=" << alignof(WalletData::PoolHashData));
    LOG_NOTICE(__FUNCTION__ << ": sizeof(WalletData::PoolsHashes)=" << sizeof(WalletData::PoolsHashes) << " alignof(WalletData::PoolsHashes)=" << alignof(WalletData::PoolsHashes));
    LOG_NOTICE(__FUNCTION__ << ": sizeof(WalletData)=" << sizeof(WalletData) << " alignof(WalletData)=" << alignof(WalletData));*/
    data_.reserve(maxWalletsNum_);
}

WalletsCache::~WalletsCache()
{
}


void WalletsCache::load(const BlockChain& bc)
{
    //LOG_NOTICE(__FUNCTION__ << ": from BlockChain: poolsNum=" << bc.getSize());
    data_.clear();

    csdb::Pool curr = bc.loadBlock(bc.getLastHash());
    while (curr.is_valid())
    {
        load(curr, Mode::Load);

        curr = bc.loadBlock(curr.previous_hash());
    }
}

void WalletsCache::updateFrom(csdb::Pool& curr)
{
    //LOG_NOTICE(__FUNCTION__ << ": from Pool");
    load(curr, Mode::Update);
}

void WalletsCache::load(csdb::Pool& curr, Mode mode)
{
    PoolHash poolHash;
    convert(curr.hash(), poolHash);
    //LOG_NOTICE(__FUNCTION__ << ": mode=" << mode << " poolHash=" << poolHash << " trxNum=" << curr.transactions_count());

    for (size_t i = 0; i < curr.transactions_count(); i++)
    {
        csdb::Transaction tr = curr.transaction(i);
        load(tr, mode, poolHash);
    }
}

void WalletsCache::load(csdb::Transaction& tr, Mode mode, const PoolHash& poolHash)
{
    loadTrxForSource(tr, mode, poolHash);
    loadTrxForTarget(tr, mode, poolHash);
}

void WalletsCache::loadTrxForSource(csdb::Transaction& tr, Mode mode, const PoolHash& poolHash)
{
    //LOG_NOTICE(__FUNCTION__);
    auto* walDataPtr = getWalletData(tr.source().public_key());
    if (!walDataPtr)
        return;
    WalletsCache::WalletData& walData = *walDataPtr;

    walData.balance_ -= tr.amount();
	walData.balance_ -= tr.counted_fee();
    addPoolHash(walData, mode, poolHash);
}

void WalletsCache::loadTrxForTarget(csdb::Transaction& tr, Mode mode, const PoolHash& poolHash)
{
    //LOG_NOTICE(__FUNCTION__);
    auto* walDataPtr = getWalletData(tr.target().public_key());
    if (!walDataPtr)
        return;
    WalletsCache::WalletData& walData = *walDataPtr;

    walData.balance_ += tr.amount();
    addPoolHash(walData, mode, poolHash);
}

WalletsCache::WalletData* WalletsCache::getWalletData(const WalletData::Address&& address)
{
    auto itWalData = data_.find(address);
    if (itWalData == data_.end())
    {
        if (data_.size() >= maxWalletsNum_)
            return nullptr;

        auto res = data_.emplace(make_pair(move(address), WalletData()));
        itWalData = res.first;
    }
    return &itWalData->second;
}

void WalletsCache::addPoolHash(WalletData& walData, Mode mode, const PoolHash& poolHash)
{
    switch (mode)
    {
    case Mode::Load:
        addPoolHashAsLoad(walData, mode, poolHash);
        break;
    case Mode::Update:
        addPoolHashAsUpdate(walData, mode, poolHash);
        break;
    default:
        // ToDo throw
        break;
    }
}

void WalletsCache::addPoolHashAsLoad(WalletData& walData, Mode mode, const PoolHash& poolHash)
{
    bool isHashEqual = (!walData.poolsHashes_.empty() && walData.poolsHashes_.front().poolHash == poolHash);

    if (!isHashEqual && !walData.poolsHashes_.full())
    {
        walData.poolsHashes_.push_front(WalletData::PoolHashData{ poolHash, 0 });
        isHashEqual = true;
    }
    if (isHashEqual &&
        walData.poolsHashes_.front().trxNum < WalletData::PoolHashData::maxTrxNum)
    {
        ++walData.poolsHashes_.front().trxNum;
    }
}

void WalletsCache::addPoolHashAsUpdate(WalletData& walData, Mode mode, const PoolHash& poolHash)
{
    bool isHashEqual = (!walData.poolsHashes_.empty() && walData.poolsHashes_.back().poolHash == poolHash);

    if (!isHashEqual)
    {
        if (walData.poolsHashes_.full())
            walData.poolsHashes_.pop_front();
        walData.poolsHashes_.push_back(WalletData::PoolHashData{ poolHash, 0 });
        isHashEqual = true;
    }
    if (isHashEqual &&
        walData.poolsHashes_.back().trxNum < WalletData::PoolHashData::maxTrxNum)
    {
        ++walData.poolsHashes_.back().trxNum;
    }
}

const WalletsCache::WalletData* WalletsCache::findWallet(const csdb::internal::byte_array&& dbAddress) const
{
    WalletData::Address walAddress = std::move(dbAddress);
    auto itWalData = data_.find(walAddress);
    if (itWalData == data_.end())
        return nullptr;
    return &itWalData->second;
}

namespace
{
    std::ostream& printHex(std::ostream& os, const char* bytes, size_t num)
    {
        char hex[] = { '0', '1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
        for (size_t i = 0; i < num; i++)
        {
            os << hex[(bytes[i] >> 4) & 0x0F];
            os << hex[bytes[i] & 0x0F];
        }
        return os;
    }

}

std::ostream& operator<<(std::ostream& os, const WalletsCache::PoolHash& poolHash)
{
    printHex(os, reinterpret_cast<const char*>(&*poolHash.begin()), poolHash.size());
    return os;
}

std::ostream& operator<<(std::ostream& os, WalletsCache::Mode mode)
{
    switch (mode)
    {
    case WalletsCache::Mode::Load: return os << "Load";
    case WalletsCache::Mode::Update: return os << "Update";
    default:
        return os << "Wrong mode=" << (int64_t)mode;
    }
}

} // namespace Credits
