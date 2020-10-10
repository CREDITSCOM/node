#pragma once

#include <cinttypes>
#include <functional>
#include <map>
#include <tuple>
#include <vector>

#include <csdb/amount.hpp>
#include <csdb/transaction.hpp>
#include <csdb/user_field.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/walletscache.hpp>
#include <lib/system/common.hpp>

namespace cs {

using Delegations = std::vector<std::tuple<PublicKey, PublicKey, csdb::TransactionID>>;
using DelegationsTiming = std::map<uint64_t, Delegations>;

class Staking {
public:
  using GetWalletDataCallback = std::function<WalletsCache::WalletData&(const PublicKey&)>;

  Staking(GetWalletDataCallback cb);

  void cleanObsoletteDelegations(uint64_t time);
  void cleanDelegationsFromCache(uint64_t delTime, Delegations& value);
  bool removeSingleDelegation(uint64_t delTime, PublicKey& first, PublicKey& second, csdb::TransactionID id);

  void addDelegations(
      const csdb::UserField&,
      const PublicKey& source,
      const PublicKey& target,
      const csdb::Amount& amount,
      const csdb::TransactionID&
  );
  void revertDelegations(
      const csdb::UserField&,
      const PublicKey& source,
      const PublicKey& target,
      const csdb::Amount& amount,
      const csdb::TransactionID&
  );

  const DelegationsTiming& getCurrentDelegations() const { return currentDelegations_; }

private:
  DelegationsTiming currentDelegations_;
  GetWalletDataCallback getWalletData_;
};

} // namespace cs
