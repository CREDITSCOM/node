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

WalletsCache::WalletsCache(const Config& config, csdb::Address genesisAddress, csdb::Address startAddress, WalletsIds& walletsIds)
    : config_(config)
    , walletsIds_(walletsIds)
    , genesisAddress_(genesisAddress)
    , startAddress_(startAddress)
{
    wallets_.reserve(config.initialWalletsNum_);
}

WalletsCache::~WalletsCache()
{
    for (auto el : wallets_)
        delete el;
}

std::unique_ptr<WalletsCache::Initer> WalletsCache::createIniter()
{
    return std::unique_ptr<Initer>(new Initer(*this));
}

std::unique_ptr<WalletsCache::Updater> WalletsCache::createUpdater()
{
    return std::unique_ptr<Updater>(new Updater(*this));
}

// Initer
WalletsCache::Initer::Initer(WalletsCache& data)
    : ProcessorBase(data)
{
    walletsSpecial_.reserve(data_.config_.initialWalletsNum_);
}

void WalletsCache::Initer::loadPrevBlock(csdb::Pool& curr)
{
    load(curr);
}

// Updater
void WalletsCache::Updater::loadNextBlock(csdb::Pool& curr)
{
    modified_.reset();
    load(curr);
}

// ProcessorBase
void WalletsCache::ProcessorBase::load(csdb::Pool& pool)
{
    const csdb::Pool::Transactions& transactions = pool.transactions();

    for (auto itTrx = transactions.crbegin(); itTrx != transactions.crend(); ++itTrx)
    {
        load(*itTrx);
    }
}

void WalletsCache::ProcessorBase::load(const csdb::Transaction& tr)
{
    loadTrxForSource(tr);
    loadTrxForTarget(tr);
}

void WalletsCache::ProcessorBase::loadTrxForSource(const csdb::Transaction& tr)
{
    csdb::Address wallAddress = tr.source();

    if (wallAddress == data_.genesisAddress_  ||  wallAddress == data_.startAddress_)
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
    setModified(id);
}

void WalletsCache::ProcessorBase::loadTrxForTarget(const csdb::Transaction& tr)
{
    csdb::Address wallAddress = tr.target();

    if (wallAddress == data_.genesisAddress_ || wallAddress == data_.startAddress_)
        return;
    WalletId id{};
    if (!findWalletId(wallAddress, id))
    {
        LOG_ERROR("Cannot find target wallet");
        return;
    }

    WalletData& wallData = getWalletData(id, tr.target());

    wallData.balance_ += tr.amount();
    setModified(id);
}

bool WalletsCache::Initer::findWalletId(const csdb::Address& address, WalletId& id)
{
    if (!data_.walletsIds_.special().findAnyOrInsertSpecial(address, id))
        return false;
    return true;
}

bool WalletsCache::Updater::findWalletId(const csdb::Address& address, WalletId& id)
{
    if (!data_.walletsIds_.normal().find(address, id))
        return false;
    return true;
}

WalletsCache::WalletData& WalletsCache::ProcessorBase::getWalletData(Data& wallets, WalletId id, const csdb::Address& address)
{
    id = WalletsIds::Special::makeNormal(id);

    if (id >= wallets.size())
        wallets.resize(id + 1);

    if (!wallets[id])
    {
        wallets[id] = new WalletData{};
        convert(address, wallets[id]->address_);
    }
    return *wallets[id];
}

WalletsCache::WalletData& WalletsCache::Initer::getWalletData(WalletId id, const csdb::Address& address)
{
    if (WalletsIds::Special::isSpecial(id))
        return ProcessorBase::getWalletData(walletsSpecial_, id, address);
    else
        return ProcessorBase::getWalletData(data_.wallets_, id, address);
}

WalletsCache::WalletData& WalletsCache::Updater::getWalletData(WalletId id, const csdb::Address& address)
{
    return ProcessorBase::getWalletData(data_.wallets_, id, address);
}

void WalletsCache::Initer::setModified(WalletId)
{
}

void WalletsCache::Updater::setModified(WalletId id)
{
    modified_.resize(data_.wallets_.size());
    if (id >= modified_.size())
    {
        LOG_ERROR("id >= modified_.size: id=" << id << " modified_.size=" << modified_.size());
        return;
    }
    modified_.set(id);
}

bool WalletsCache::Initer::moveData(WalletId srcIdSpecial, WalletId destIdNormal)
{
    if (!WalletsIds::Special::isSpecial(srcIdSpecial))
        return false;
    srcIdSpecial = WalletsIds::Special::makeNormal(srcIdSpecial);

    if (srcIdSpecial >= walletsSpecial_.size())
        return false;
    if (!walletsSpecial_[srcIdSpecial])
    {
        LOG_ERROR("Src wallet data should not be empty");
        return false;
    }

    if (destIdNormal >= data_.wallets_.size())
        data_.wallets_.resize(destIdNormal + 1);
    if (data_.wallets_[destIdNormal])
    {
        LOG_ERROR("Dest wallet data should be empty");
        return false;
    }
    data_.wallets_[destIdNormal] = walletsSpecial_[srcIdSpecial];
    walletsSpecial_[srcIdSpecial] = nullptr;
    return true;
}

const WalletsCache::WalletData* WalletsCache::Updater::findWallet(WalletId id) const
{
    if (id >= data_.wallets_.size())
        return nullptr;
    return data_.wallets_[id];
}

} // namespace Credits
