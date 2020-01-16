#ifndef WALLETS_STATE_HPP
#define WALLETS_STATE_HPP

#include <unordered_map>

#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/internal/types.hpp>
#include <csnode/transactionstail.hpp>
#include <csnode/walletscache.hpp>

namespace cs {
class WalletsCache;
class WalletsIds;

class WalletsState {
public:
    using WalletAddress = csdb::Address;
    using TransactionIndex = uint32_t;

    static constexpr TransactionIndex noInd_ = std::numeric_limits<TransactionIndex>::max();

    struct WalletData {
        TransactionIndex lastTrxInd_{};
        csdb::Amount balance_{};
        csdb::Amount delegated_{};
        std::map<cs::PublicKey, csdb::Amount> delegats_;
        TransactionsTail trxTail_{};
    };

    explicit WalletsState(const WalletsCache::Updater& cacheUpd) : wallCache_(cacheUpd) {}
    WalletData& getData(const WalletAddress& address);

    void updateFromSource() { storage_.clear(); }

private:
    const WalletsCache::Updater& wallCache_;
    std::unordered_map<PublicKey, WalletData> storage_;
};
}  // namespace cs
#endif // WALLETS_STATE_HPP
