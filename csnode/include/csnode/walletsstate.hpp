#ifndef WALLETS_STATE_HPP
#define WALLETS_STATE_HPP

#include <memory>
#include <unordered_map>

#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/internal/types.hpp>
#include <csnode/transactionstail.hpp>
#include <csnode/walletscache.hpp>

namespace std {
template<>
class hash<cs::PublicKey> {
public:
    size_t operator()(const cs::PublicKey& obj) const {
        static_assert(sizeof(size_t) < sizeof(cs::PublicKey));
        size_t res;
        std::copy(obj.data(), obj.data() + sizeof(res), &res);
        return res;
    }
};
} // namespace std

class BlockChain;

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
        TransactionsTail trxTail_{};
    };

    explicit WalletsState(const BlockChain&);
    WalletData& getData(const WalletAddress& address);

    void updateFromSource();

private:
    std::shared_ptr<WalletsCache::Updater> cacheSptr_;
    std::unordered_map<PublicKey, WalletData> storage_;
};
}  // namespace cs

#endif // WALLETS_STATE_HPP
