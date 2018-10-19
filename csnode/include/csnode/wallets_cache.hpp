#ifndef WALLETS_CACHE_H
#define WALLETS_CACHE_H

#include <array>
#include <limits>
#include <ostream>
#include <vector>
#include <unordered_map>
#include <boost/functional/hash.hpp>
#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/pool.h>
#include <csdb/transaction.h>

#include <csnode/cyclic_buffer.hpp>

namespace std
{
    template<typename T>
    struct hash<vector<T>>
    {
        using Vec = vector<T>;
        size_t operator()(const Vec& vec) const
        {
            return boost::hash_range(vec.begin(), vec.end());
        }
    };
}

namespace csdb
{
    class Pool;
    class Transaction;
}

class BlockChain;

namespace Credits
{

    class WalletsCache
    {
    public:
        struct Config
        {
            size_t memSize = 1024LL * 1024 * 1024;// actual mem could be slightly bigger
                                                  // 256K wallets (4K for each wallet)
        };
        using PoolHash = std::array<uint8_t, 32>;
        friend std::ostream& operator<<(std::ostream& os, const PoolHash& poolHash);

        void unUpdateFrom(csdb::Pool&);

    public:
        struct WalletData
        {
            using Address = std::vector<uint8_t>; // = csdb::internal::byte_array;
            struct PoolHashData
            {
                PoolHash poolHash;
                uint32_t trxNum;
                static constexpr size_t maxTrxNum = std::numeric_limits<uint32_t>::max();
            };
            static constexpr size_t maxPoolsHashesNum_ = 100;
            using PoolsHashes = CyclicBuffer<PoolHashData, maxPoolsHashesNum_>;

            PoolsHashes poolsHashes_;
            csdb::Amount balance_;
        };

    public:
        static void convert(const csdb::PoolHash& poolHashDb, PoolHash& poolHashCache);
        static void convert(const PoolHash& poolHashCache, csdb::PoolHash& poolHashDb);
    public:
        WalletsCache(const Config& config);
        ~WalletsCache();

        void load(const BlockChain& bc);
        void updateFrom(csdb::Pool& curr);

        const WalletData* findWallet(const csdb::internal::byte_array&& dbAddress) const;

    private:
        enum class Mode : uint8_t
        {
            Load,
            Update
        };
        friend std::ostream& operator<<(std::ostream& os, Mode mode);

        void load(csdb::Pool& curr, Mode mode);
        void load(csdb::Transaction& tr, Mode mode, const PoolHash& poolHash, WalletsCache::WalletData& walWriter);
        void loadTrxForSource(csdb::Transaction& tr, Mode mode, const PoolHash& poolHash, WalletsCache::WalletData& walWriter);
        void loadTrxForTarget(csdb::Transaction& tr, Mode mode, const PoolHash& poolHash);
        WalletData* getWalletData(const WalletData::Address&& address);

        void addPoolHash(WalletData& walData, Mode mode, const PoolHash& poolHash);
        void addPoolHashAsLoad(WalletData& walData, Mode mode, const PoolHash& poolHash);
        void addPoolHashAsUpdate(WalletData& walData, Mode mode, const PoolHash& poolHash);

    private:
        Config config_;
        const size_t maxWalletsNum_;

        using Data = std::unordered_map<WalletData::Address, WalletData>;
        Data data_;
    };

} // namespace Credits

#endif
