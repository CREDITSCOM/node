#ifndef WALLETS_CACHE_HPP
#define WALLETS_CACHE_HPP

#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#include <cscrypto/cscrypto.hpp>
#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/transactionstail.hpp>

#include <lib/system/common.hpp>
#include <lib/system/signals.hpp>

namespace std {
template<>
class hash<cs::PublicKey> {
public:
    size_t operator()(const cs::PublicKey& obj) const {
        static_assert(sizeof(size_t) < sizeof(cs::PublicKey));
        size_t res;
        std::copy(obj.data(), obj.data() + sizeof(res), reinterpret_cast<uint8_t*>(&res));
        return res;
    }
};
} // namespace std

class BlockChain;

namespace csdb {
class Pool;
class Transaction;
}  // namespace csdb

namespace cs {

class WalletsIds;

class WalletsCache {
public:
    WalletsCache(WalletsIds& walletsIds);

    WalletsCache(const WalletsCache&) = delete;
    WalletsCache& operator=(const WalletsCache&) = delete;
    WalletsCache(const WalletsCache&&) = delete;
    WalletsCache& operator=(const WalletsCache&&) = delete;

    class Updater;
    std::unique_ptr<Updater> createUpdater();

    struct WalletData {
        csdb::Amount balance_;
        TransactionsTail trxTail_;
		uint64_t transNum_ = 0;
        csdb::TransactionID lastTransaction_;
#ifdef MONITOR_NODE
        uint64_t createTime_ = 0;        
#endif
    };

    struct TrustedData {
        uint64_t times = 0;
        uint64_t times_trusted = 0;
        csdb::Amount totalFee;
    };

    void iterateOverWallets(const std::function<bool(const PublicKey&, const WalletData&)>);

#ifdef MONITOR_NODE
    void iterateOverWriters(const std::function<bool(const PublicKey&, const TrustedData&)>);
#endif

    uint64_t getCount() const {
        return wallets_.size();
    }

private:
    WalletsIds& walletsIds_;

    std::list<csdb::TransactionID> smartPayableTransactions_;
    std::map< csdb::Address, std::list<csdb::TransactionID> > canceledSmarts_;
    std::unordered_map<PublicKey, WalletData> wallets_;

#ifdef MONITOR_NODE
    std::map<PublicKey, TrustedData> trusted_info_;
#endif
};

using WalletUpdateSignal = cs::Signal<void(const PublicKey&, const WalletsCache::WalletData&)>;
using FinishedUpdateFromDB =
    cs::Signal<void(const std::unordered_map<PublicKey, WalletsCache::WalletData>&)>;

class WalletsCache::Updater {
public:
    Updater(WalletsCache& data);

    void loadNextBlock(csdb::Pool& curr,
                       const cs::ConfidantsKeys& confidants,
                       const BlockChain& blockchain,
                       bool inverse = false); // inverse all operations

    const WalletData* findWallet(const PublicKey&) const;
    const WalletData* findWallet(const csdb::Address&) const;

    void invokeReplenishPayableContract(const csdb::Transaction&, bool inverse = false);

    void rollbackExceededTimeoutContract(const csdb::Transaction&,
                                         const csdb::Amount& execFee,
                                         bool inverse = false);

    void smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx,
                                        const csdb::Transaction& initTrx,
                                        bool inverse = false);

    void updateLastTransactions(const std::vector<std::pair<PublicKey, csdb::TransactionID>>&);

    PublicKey toPublicKey(const csdb::Address&) const;

    void onStopReadingFromDB() const {
      emit updateFromDBFinishedEvent(data_.wallets_);
    }

public signals:
    WalletUpdateSignal walletUpdateEvent;
    FinishedUpdateFromDB updateFromDBFinishedEvent;

private:
    WalletData& getWalletData(const PublicKey&);
    WalletData& getWalletData(const csdb::Address&);

    double load(const csdb::Transaction& tr, const BlockChain& blockchain, bool inverse);

    double loadTrxForSource(const csdb::Transaction& tr,
                            const BlockChain& blockchain,
                            bool inverse);
    void loadTrxForTarget(const csdb::Transaction& tr, bool inverse);

    void fundConfidantsWalletsWithFee(const csdb::Amount& totalFee,
                                      const cs::ConfidantsKeys& confidants,
                                      const std::vector<uint8_t>& realTrusted,
                                      bool inverse);
    void fundConfidantsWalletsWithExecFee(const csdb::Transaction& transaction,
                                          const BlockChain& blockchain,
                                          bool inverse);

    void checkSmartWaitingForMoney(const csdb::Transaction& initTransaction,
                                   const csdb::Transaction& newStateTransaction,
                                   bool inverse);
    bool isCanceledSmart(const csdb::Address& contract_addr, const csdb::TransactionID& tid);
    void checkCanceledSmart(const csdb::Address& contract_addr,
                            const csdb::TransactionID& tid,
                            bool inverse);

#ifdef MONITOR_NODE
    bool setWalletTime(const PublicKey& address, const uint64_t& p_timeStamp);
#endif

    WalletsCache& data_;
};

inline const WalletsCache::WalletData* WalletsCache::Updater::findWallet(const PublicKey& key) const {
    auto it = data_.wallets_.find(key);
    if (it == data_.wallets_.end()) {
        return nullptr;
    }
    return &(it->second);
}

inline const WalletsCache::WalletData* WalletsCache::Updater::findWallet(const csdb::Address& addr) const {
    return findWallet(toPublicKey(addr));
}

inline WalletsCache::WalletData& WalletsCache::Updater::getWalletData(const PublicKey& key) {
    return data_.wallets_[key];
}

inline WalletsCache::WalletData& WalletsCache::Updater::getWalletData(const csdb::Address& addr) {
    return data_.wallets_[toPublicKey(addr)];
}

inline double WalletsCache::Updater::load(const csdb::Transaction& t, const BlockChain& bc, bool inverse) {
    loadTrxForTarget(t, inverse);
    return loadTrxForSource(t, bc, inverse);
}
} // namespace cs
#endif // WALLETS_CACHE_HPP
