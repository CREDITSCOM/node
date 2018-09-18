#include <csnode/blockchain.hpp>
#include <Solver/Fake/WalletsState.h>

namespace Credits
{

    WalletsState::WalletsState(const BlockChain& blockchain, size_t initialWalletsNum /*=*/)
        : blockchain_(blockchain)
        , wallExisting_(blockchain, initialWalletsNum)
    {
    }

    WalletsState::WalletsExisting::WalletsExisting(const BlockChain& blockchain, size_t initialWalletsNum /*=*/)
        : blockchain_(blockchain)
    {
        modified_.resize(initialWalletsNum, true);
        storage_.resize(initialWalletsNum);
        for (auto& el : storage_)
            el = new WalletData{};
    }

    WalletsState::WalletsExisting::~WalletsExisting()
    {
        for (auto& el : storage_)
            delete el;
        storage_.clear();
    }

    void WalletsState::WalletsExisting::updateFromSource()
    {
        blockchain_.getModifiedWallets(toCopy_);
        modified_.resize(toCopy_.size(), false);
        toCopy_ |= modified_;
        modified_.reset();

        if (storage_.size() < toCopy_.size())
            storage_.resize(toCopy_.size());
    }

    bool WalletsState::WalletsExisting::updateFromSource(const WalletId& id)
    {
        if (id >= storage_.size())
            return false;

        BlockChain::WalletData walletData{};
        if (!blockchain_.findWalletData(id, walletData))
            return false;

        if (storage_[id])
        {
            storage_[id]->balance_ = walletData.balance_;
            storage_[id]->trxTail_ = walletData.trxTail_;
        }
        else
        {
            storage_[id] = new WalletData{ walletData.balance_ , walletData.trxTail_ };
        }
        toCopy_.reset(id);
        return true;
    }

    WalletsState::WalletData* WalletsState::WalletsExisting::getData(const WalletId& id)
    {
        if (id >= toCopy_.size())
            return nullptr;
        if (toCopy_[id] && !updateFromSource(id))
            return nullptr;
        return storage_[id];
    }

    void WalletsState::WalletsExisting::setModified(const WalletId& id)
    {
        if (id < modified_.size())
            modified_.set(id);
    }

    void WalletsState::WalletsNew::clear()
    {
        storage_.clear();
    }

    WalletsState::WalletData& WalletsState::WalletsNew::getData(const WalletAddress& address)
    {
        return storage_[address];
    }

    void WalletsState::updateFromSource()
    {
        wallNew_.clear();
        wallExisting_.updateFromSource();
    }

    WalletsState::WalletData& WalletsState::getData(const WalletAddress& address, WalletId& id)
    {
        id = noWalletId;

        if (blockchain_.findWalletId(address, id))
        {
            WalletData* res = wallExisting_.getData(id);
            if (res)
                return *res;
        }
        return wallNew_.getData(address);
    }

    void WalletsState::setModified(const WalletId& id)
    {
        if (id != noWalletId)
            wallExisting_.setModified(id);
    }
}