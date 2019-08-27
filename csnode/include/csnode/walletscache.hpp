#ifndef WALLETS_CACHE_HPP
#define WALLETS_CACHE_HPP

#include <list>
#include <memory>
#include <unordered_map>

#include <cscrypto/cscrypto.hpp>
#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/transactionstail.hpp>

#include <lib/system/common.hpp>

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

    struct RefContractCall {
        cs::Sequence sequence;
        uint32_t transaction;
    };

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
    std::map<csdb::Address, std::list<RefContractCall>> canceledSmarts_;
    std::unordered_map<PublicKey, WalletData> wallets_;

#ifdef MONITOR_NODE
    std::map<PublicKey, TrustedData> trusted_info_;
#endif
};

class WalletsCache::Updater {
public:
    Updater(WalletsCache& data);

    void loadNextBlock(csdb::Pool& curr, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain);

    const WalletData* findWallet(const PublicKey&) const;
    const WalletData* findWallet(const csdb::Address&) const;

    void invokeReplenishPayableContract(const csdb::Transaction&);
    void rollbackExceededTimeoutContract(const csdb::Transaction&, const WalletsCache::RefContractCall&, const csdb::Amount& execFee = 0);
    void smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx, const csdb::Transaction& initTrx);

    PublicKey toPublicKey(const csdb::Address&) const;

private:
    WalletData& getWalletData(const PublicKey&);
    WalletData& getWalletData(const csdb::Address&);

    double load(const csdb::Transaction& tr, const BlockChain& blockchain);
    double loadTrxForSource(const csdb::Transaction& tr, const BlockChain& blockchain);
    void loadTrxForTarget(const csdb::Transaction& tr);

    void fundConfidantsWalletsWithFee(const csdb::Amount& totalFee, const cs::ConfidantsKeys& confidants, const std::vector<uint8_t>& realTrusted);
    void fundConfidantsWalletsWithExecFee(const csdb::Transaction& transaction, const BlockChain& blockchain);

    void checkSmartWaitingForMoney(const csdb::Transaction& initTransaction, const WalletsCache::RefContractCall& initRef, const csdb::Transaction& newStateTransaction);
    bool isCanceledSmart(const csdb::Address& contract_addr, const WalletsCache::RefContractCall& ref);
    void checkCanceledSmart(const csdb::Address& contract_addr, const WalletsCache::RefContractCall& ref);

#ifdef MONITOR_NODE
    bool setWalletTime(const PublicKey& address, const uint64_t& p_timeStamp);
#endif

    WalletsCache& data_;
};
} // namespace cs
#endif // WALLETS_CACHE_HPP
