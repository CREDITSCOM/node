#ifndef WALLETS_CACHE_H
#define WALLETS_CACHE_H

#include <ostream>
#include <string>
#include <vector>
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

        void loadPrevBlock(csdb::Pool& curr);
        void loadNextBlock(csdb::Pool& curr);

        const WalletData* findWallet(const WalletId& id) const;

    private:
        void load(csdb::Pool& curr);
        void load(const csdb::Transaction& tr);
        void loadTrxForSource(const csdb::Transaction& tr);
        void loadTrxForTarget(const csdb::Transaction& tr);
        bool findWalletId(const csdb::Address& address, WalletId& id) const;
        WalletsCache::WalletData& getWalletData(const WalletId& id, const csdb::Address& address);

    private:
        const Config config_;
        const WalletsIds& walletsIds_;
        const csdb::Address genesisAddress_;
        const csdb::Address startAddress_;

        using Data = std::vector<WalletData*>;
        Data data_;
    };

} // namespace Credits

#endif
