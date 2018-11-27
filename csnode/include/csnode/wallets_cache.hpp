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

#include <client/params.hpp>
#include <map>

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

namespace cs
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

#ifdef MONITOR_NODE
            uint64_t createTime_ = 0;
            uint64_t transNum_ = 0;
#endif
#ifdef TRANSACTIONS_INDEX
          csdb::TransactionID lastTransaction_;
#endif
        };

        struct WriterData {
          uint64_t times = 0;
          csdb::Amount totalFee;
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
        void iterateOverWallets(const std::function<bool(const WalletData::Address&, const WalletData&)>);

#ifdef MONITOR_NODE
        void iterateOverWriters(const std::function<bool(const WalletData::Address&, const WriterData&)>);
#endif

    private:
        enum class Mode : uint8_t
        {
            Load,
            Update
        };
        friend std::ostream& operator<<(std::ostream& os, Mode mode);

        void load(csdb::Pool& curr, Mode mode);
        void load(csdb::Transaction& tr, Mode mode, const PoolHash& poolHash, WalletsCache::WalletData& walWriter, const uint64_t timeStamp);
        void loadTrxForSource(csdb::Transaction& tr, Mode mode, const PoolHash& poolHash, WalletsCache::WalletData& walWriter, const uint64_t timeStamp);
        void loadTrxForTarget(csdb::Transaction& tr, Mode mode, const PoolHash& poolHash, const uint64_t timeStamp);
        WalletData* getWalletData(const WalletData::Address&& address, const uint64_t timeStamp);

        void addPoolHash(WalletData& walData, Mode mode, const PoolHash& poolHash);
        void addPoolHashAsLoad(WalletData& walData, Mode mode, const PoolHash& poolHash);
        void addPoolHashAsUpdate(WalletData& walData, Mode mode, const PoolHash& poolHash);

    private:
        Config config_;
        const size_t maxWalletsNum_;

        using Data = std::unordered_map<WalletData::Address, WalletData>;
        Data data_;
#ifdef MONITOR_NODE
        std::map<WalletData::Address, WriterData> writers_;
#endif
    };

} // namespace cs

#endif
