#pragma once

#include <cinttypes>
#include <functional>
#include <map>
#include <tuple>
#include <vector>

#include <csdb/transaction.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/walletscache.hpp>
#include <lib/system/common.hpp>

namespace cs {

using Delegations = std::vector<std::tuple<PublicKey, PublicKey, csdb::TransactionID>>;
using DelegationsTiming = std::map<uint64_t, Delegations>;

class Staking {
public:
  using GetWalletDataCallback = std::function<WalletsCache::WalletData&(const PublicKey&)>;

  Staking(GetWalletDataCallback cb) : getWalletData_(cb) {}

  void cleanObsoletteDelegations(uint64_t time);
  void cleanDelegationsFromCache(uint64_t delTime, Delegations& value);
  bool removeSingleDelegation(uint64_t delTime, PublicKey& first, PublicKey& second, csdb::TransactionID id);

  DelegationsTiming& getCurrentDelegations() { return currentDelegations_; }

private:
  DelegationsTiming currentDelegations_;
  GetWalletDataCallback getWalletData_;
};

} // namespace cs
