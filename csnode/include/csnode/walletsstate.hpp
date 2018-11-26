#ifndef WALLETS_STATE_HPP
#define WALLETS_STATE_HPP

#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/internal/types.h>
#include <boost/dynamic_bitset.hpp>
#include <csnode/transactionstail.hpp>
#include <unordered_map>
#include <vector>

class BlockChain;

namespace cs {
class WalletsCache;
class WalletsIds;

class WalletsState {
public:
  using WalletAddress = csdb::Address;
  using WalletId = csdb::internal::WalletId;
  using Mask = boost::dynamic_bitset<uint64_t>;
  using TransactionIndex = uint32_t;
  static constexpr TransactionIndex noInd_ = std::numeric_limits<TransactionIndex>::max();
  static constexpr WalletId noWalletId_ = 0;

public:
  struct WalletData {
    TransactionIndex lastTrxInd_{};
    csdb::Amount balance_{};
    TransactionsTail trxTail_{};
  };

public:
  explicit WalletsState(const BlockChain& blockchain, size_t initialWalletsNum = 2 * 1024 * 1024);

  void updateFromSource();
  WalletData& getData(const WalletAddress& address, WalletId& id);
  void setModified(const WalletId& id);

private:
  class WalletsExisting {
public:
    explicit WalletsExisting(const BlockChain& blockchain, size_t initialWalletsNum = 2 * 1024 * 1024);
    ~WalletsExisting();

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

  class WalletsNew {
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
}  // namespace cs

#endif
