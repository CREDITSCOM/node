#ifndef WALLETS_STATE_H
#define WALLETS_STATE_H

#include <vector>
#include <unordered_map>
#include <boost/dynamic_bitset.hpp>
#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/internal/types.h>
#include <csnode/TransactionsTail.h>

class BlockChain;

namespace Credits
{
    class WalletsCache;
    class WalletsIds;

    class WalletsState
    {
    public:
        using WalletAddress = csdb::Address;
        using WalletId = csdb::internal::WalletId;
        using Mask = boost::dynamic_bitset<uint64_t>;
    public:
        struct WalletData
        {
            csdb::Amount balance_;
            TransactionsTail trxTail_;
        };

        static constexpr WalletId noWalletId = 0;

    public:
        WalletsState(const BlockChain& blockchain, size_t initialWalletsNum = 2 * 1024 * 1024)
          : blockchain_(blockchain),
            wallExisting_(blockchain, initialWalletsNum)
        {}

        void updateFromSource();
        WalletData& getData(const WalletAddress& address, WalletId& id);
        void setModified(const WalletId& id);
    private:
        class WalletsExisting
        {
        public:
            WalletsExisting(const BlockChain& blockchain, size_t initialWalletsNum = 2 * 1024 * 1024)
              : blockchain_(blockchain)
            {}
            ~WalletsExisting() {};

            void updateFromSource();
            WalletData* getData(const WalletId& id);
            void setModified(const WalletId& id);
        private:
            bool updateFromSource(const WalletId& id);
        private:
            const BlockChain& blockchain_;
            using Storage = std::vector<WalletData*>;
            Storage storage_;
            Mask toCopy_;
            Mask modified_;
        };

        class WalletsNew
        {
        public:
            void clear();
            WalletData& getData(const WalletAddress& address);
        private:
            using Storage = std::unordered_map<WalletAddress, WalletData>;
            Storage storage_;
        };

    private:
        const BlockChain& blockchain_;
        WalletsExisting wallExisting_;
        WalletsNew wallNew_;
    };
}

#endif
