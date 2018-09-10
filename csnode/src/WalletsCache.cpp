#include <algorithm>
#include <lib/system/logger.hpp>
#include <csnode/WalletsIds.h>
#include <csnode/WalletsCache.h>

using namespace std;

namespace Credits
{
void WalletsCache::convert(const csdb::Address& address, WalletData::Address& walletAddress)
{
    const csdb::internal::byte_array& addrVec = address.public_key();
    copy(addrVec.begin(), addrVec.end(), walletAddress.begin());
}

void WalletsCache::convert(const WalletData::Address& walletAddress, csdb::Address& address)
{
    csdb::internal::byte_array hashBytes(walletAddress.begin(), walletAddress.end());
    address = csdb::Address::from_public_key(hashBytes);
}

WalletsCache::WalletsCache(const Config& config, csdb::Address genesisAddress, csdb::Address startAddress, const WalletsIds& walletsIds)
    : config_(config)
    , walletsIds_(walletsIds)
    , genesisAddress_(genesisAddress)
    , startAddress_(startAddress)
{
    data_.reserve(config.initialWalletsNum_);
}

WalletsCache::~WalletsCache()
{
    for (auto el : data_)
        delete el;
}

void WalletsCache::loadPrevBlock(csdb::Pool& curr)
{
    load(curr);
}

void WalletsCache::loadNextBlock(csdb::Pool& curr)
{
    //LOG_NOTICE(__FUNCTION__ << ": from Pool");
    load(curr);
}

void WalletsCache::load(csdb::Pool& pool)
{
    modified_.reset();

    const csdb::Pool::Transactions& transactions = pool.transactions();

    for (auto itTrx = transactions.crbegin(); itTrx != transactions.crend(); ++itTrx)
    {
        load(*itTrx);
    }
}

void WalletsCache::load(const csdb::Transaction& tr)
{
    loadTrxForSource(tr);
    loadTrxForTarget(tr);
}

void WalletsCache::loadTrxForSource(const csdb::Transaction& tr)
{
    csdb::Address wallAddress = tr.source();

    if (wallAddress == genesisAddress_  ||  wallAddress == startAddress_)
        return;
    WalletId id{};
    if (!findWalletId(wallAddress, id))
    {
        LOG_ERROR("Cannot find source wallet");
        return;
    }

    WalletData& wallData = getWalletData(id, tr.source());

    wallData.balance_ -= tr.amount();
    wallData.trxTail_.push(tr.innerID());
    modified_.set(id);
}

void WalletsCache::loadTrxForTarget(const csdb::Transaction& tr)
{
    csdb::Address wallAddress = tr.target();

    if (wallAddress == genesisAddress_ || wallAddress == startAddress_)
        return;
    WalletId id{};
    if (!findWalletId(wallAddress, id))
    {
        LOG_ERROR("Cannot find target wallet");
        return;
    }

    WalletData& wallData = getWalletData(id, tr.target());

    wallData.balance_ += tr.amount();
    modified_.set(id);
}

bool WalletsCache::findWalletId(const csdb::Address& address, WalletId& id) const
{
    if (!walletsIds_.find(address, id))
        return false;
    return true;
}

WalletsCache::WalletData& WalletsCache::getWalletData(const WalletId& id, const csdb::Address& address)
{
    if (id >= data_.size())
        data_.resize(id + 1);

    if (!data_[id])
    {
        data_[id] = new WalletData{};
        convert(address, data_[id]->address_);
    }
    return *data_[id];
}

const WalletsCache::WalletData* WalletsCache::findWallet(const WalletId& id) const
{
    if (id >= data_.size())
        return nullptr;
    return data_[id];
}

} // namespace Credits
