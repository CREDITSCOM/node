#ifndef WALLETS_CACHE_HPP
#define WALLETS_CACHE_HPP

#include <list>
#include <memory>
#include <unordered_map>
#include <vector>
#include <tuple>

#include <cscrypto/cscrypto.hpp>
#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/transactionstail.hpp>

#include <lib/system/common.hpp>
#include <lib/system/signals.hpp>

class BlockChain;

namespace csdb {
class Pool;
class Transaction;
}  // namespace csdb

namespace cs {

class WalletsIds;
class Staking;
class MultiWallets;

class WalletsCache {
public:
    WalletsCache(WalletsIds& walletsIds);
    ~WalletsCache();

    WalletsCache(const WalletsCache&) = delete;
    WalletsCache& operator=(const WalletsCache&) = delete;
    WalletsCache(const WalletsCache&&) = delete;
    WalletsCache& operator=(const WalletsCache&&) = delete;

    class Updater;
    std::unique_ptr<Updater> createUpdater();

    struct WalletData {
        PublicKey key_;
        csdb::Amount balance_ = 0;
        csdb::Amount delegated_ = 0;
        std::shared_ptr<std::map<cs::PublicKey, std::vector<cs::TimeMoney>>> delegateSources_;
        std::shared_ptr<std::map<cs::PublicKey, std::vector<cs::TimeMoney>>> delegateTargets_;
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

    uint64_t getCount() const;

    const MultiWallets& multiWallets() const { return *(multiWallets_.get()); }

private:
    WalletsIds& walletsIds_;

    std::list<csdb::TransactionID> smartPayableTransactions_;
    std::map< csdb::Address, std::list<csdb::TransactionID> > canceledSmarts_;
    std::unique_ptr<MultiWallets> multiWallets_;

#ifdef MONITOR_NODE
    std::map<PublicKey, TrustedData> trusted_info_;
#endif

    std::unique_ptr<Staking> staking_;

    friend class WalletsCache_Serializer;
};

class WalletsCache::Updater {
public:
    Updater(WalletsCache& data);
    ~Updater();

    void loadNextBlock(const csdb::Pool& curr,
                       const cs::ConfidantsKeys& confidants,
                       const BlockChain& blockchain,
                       bool inverse = false); // inverse all operations


    std::unique_ptr<WalletData> findWallet(const PublicKey&) const;
    std::unique_ptr<WalletData> findWallet(const csdb::Address&) const;

    void invokeReplenishPayableContract(const csdb::Transaction&, bool inverse = false);

    void rollbackExceededTimeoutContract(const csdb::Transaction&,
                                         const csdb::Amount& execFee,
                                         bool inverse = false);

    void smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx,
                                        const csdb::Transaction& initTrx,
                                        bool inverse = false);

    void updateLastTransactions(const std::vector<std::pair<PublicKey, csdb::TransactionID>>&);
    void logOperation(const std::string& reason, const cs::PublicKey& key, const csdb::Amount& sum);


    PublicKey toPublicKey(const csdb::Address&) const;

private:
    WalletData getWalletData(const PublicKey&);
    WalletData getWalletData(const csdb::Address&);

    double load(const csdb::Transaction& tr, const BlockChain& blockchain, const uint64_t timeStamp, bool inverse);

    double loadTrxForSource(const csdb::Transaction& tr,
                            const BlockChain& blockchain,
                            const uint64_t timeStamp,
                            bool inverse);
    void loadTrxForTarget(const csdb::Transaction& tr, const uint64_t timeStamp, bool inverse);

    void fundConfidantsWalletsWithFee(const csdb::Amount& totalFee,
                                      const cs::ConfidantsKeys& confidants,
                                      const std::vector<uint8_t>& realTrusted,
                                      const cs::Sequence poolSeq,
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
    cs::PublicKey showBalanceChangeKey_;
    bool showBalanceChange_ = false;
};

inline double WalletsCache::Updater::load(const csdb::Transaction& t, const BlockChain& bc, const uint64_t timeStamp, bool inverse) {
    loadTrxForTarget(t, timeStamp, inverse);
    return loadTrxForSource(t, bc, timeStamp, inverse);
}
} // namespace cs
#endif // WALLETS_CACHE_HPP
