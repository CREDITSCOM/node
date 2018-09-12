#ifndef WALLETS_CACHE_H
#define WALLETS_CACHE_H

#include <vector>
#include <boost/dynamic_bitset.hpp>
#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/pool.h>
#include <csdb/transaction.h>
#include "TransactionsTail.h"

namespace csdb
{
    class Pool;
    class Transaction;
}

namespace Credits
{
    class WalletsIds;

    class WalletsCache
    {
    public:
        using WalletId = csdb::internal::WalletId;
        using Mask = boost::dynamic_bitset<uint64_t>;

        struct Config
        {
            size_t initialWalletsNum_ = 2 * 1024 * 1024;
        };

    public:
        struct WalletData
        {
            using Address = std::array<uint8_t, 32>;

            Address address_;
            csdb::Amount balance_;
            TransactionsTail trxTail_;
        };

    public:
        static void convert(const csdb::Address& address, WalletData::Address& walletAddress);
        static void convert(const WalletData::Address& walletAddress, csdb::Address& address);
    public:
        WalletsCache(const Config& config, csdb::Address genesisAddress, csdb::Address startAddress, const WalletsIds& walletsIds);
        ~WalletsCache();
        WalletsCache(const WalletsCache&) = delete;
        WalletsCache& operator=(const WalletsCache&) = delete;
        WalletsCache(const WalletsCache&&) = delete;
        WalletsCache& operator=(const WalletsCache&&) = delete;

        void loadPrevBlock(csdb::Pool& curr);
        void loadNextBlock(csdb::Pool& curr);

        const WalletData* findWallet(const WalletId& id) const;
        bool findWalletId(const csdb::Address& address, WalletId& id) const;
        const Mask& getModified() const {return modified_; }

    private:
        void load(csdb::Pool& curr);
        void load(const csdb::Transaction& tr);
        void loadTrxForSource(const csdb::Transaction& tr);
        void loadTrxForTarget(const csdb::Transaction& tr);
        WalletsCache::WalletData& getWalletData(const WalletId& id, const csdb::Address& address);

    private:
        const Config config_;
        const WalletsIds& walletsIds_;
        const csdb::Address genesisAddress_;
        const csdb::Address startAddress_;

        using Data = std::vector<WalletData*>;
        Data data_;
        Mask modified_;
    };

} // namespace Credits

#endif
