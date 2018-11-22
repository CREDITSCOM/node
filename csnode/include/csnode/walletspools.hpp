#ifndef WALLETS_POOLS_HPP
#define WALLETS_POOLS_HPP

#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/internal/types.h>
#include <csdb/pool.h>
#include <array>
#include <csnode/CyclicBuffer.hpp>
#include <limits>
#include <memory>
#include <vector>

namespace csdb {
class Pool;
class Transaction;
}  // namespace csdb

namespace cs {
class WalletsIds;

class WalletsPools {
public:
  using WalletId = csdb::internal::WalletId;
  using PoolHash = std::array<uint8_t, 32>;

public:
  struct WalletData {
    struct PoolHashData {
      PoolHash poolHash;
      uint32_t trxNum;
      static constexpr size_t maxTrxNum = std::numeric_limits<uint32_t>::max();
    };
    static constexpr size_t maxPoolsHashesNum_ = 100;
    using PoolsHashes = CyclicBuffer<PoolHashData, maxPoolsHashesNum_>;

    PoolsHashes poolsHashes_;
  };

public:
  static void convert(const csdb::PoolHash& poolHashDb, PoolHash& poolHashCache);
  static void convert(const PoolHash& poolHashCache, csdb::PoolHash& poolHashDb);

public:
  WalletsPools(csdb::Address genesisAddress, csdb::Address startAddress, const WalletsIds& walletsIds);
  ~WalletsPools();
  WalletsPools(const WalletsPools&) = delete;
  WalletsPools& operator=(const WalletsPools&) = delete;
  WalletsPools(const WalletsPools&&) = delete;
  WalletsPools& operator=(const WalletsPools&&) = delete;

  void addWallet(WalletId id);
  void loadPrevBlock(csdb::Pool& curr);
  void loadNextBlock(csdb::Pool& curr);

  const WalletData* findWallet(WalletId id) const;

private:
  void* impl_;
};

}  // namespace cs

#endif
