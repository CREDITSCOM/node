#ifndef WALLETS_CACHE_HPP
#define WALLETS_CACHE_HPP

#include <boost/dynamic_bitset.hpp>
#include <cscrypto/cscrypto.hpp>
#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/transactionstail.hpp>
#include <list>
#include <map>
#include <memory>
#include <vector>

#include <lib/system/common.hpp>

class BlockChain;

namespace csdb {
class Pool;
class Transaction;
}  // namespace csdb

namespace cs {

class WalletsIds;
constexpr size_t InitialWalletsNum = 1 * 24 * 1024;

class WalletsCache {
public:
    using WalletId = csdb::internal::WalletId;
    using Mask = boost::dynamic_bitset<uint64_t>;

    struct Config {
        size_t initialWalletsNum_ = InitialWalletsNum;
    };

    WalletsCache(const Config& config, csdb::Address genesisAddress, csdb::Address startAddress, WalletsIds& walletsIds);
    ~WalletsCache();

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
        using Address = cs::PublicKey;

        Address address_;
        csdb::Amount balance_;
        TransactionsTail trxTail_;
		uint64_t transNum_ = 0;

#ifdef MONITOR_NODE
        uint64_t createTime_ = 0;        
#endif
        csdb::TransactionID lastTransaction_;
    };

    struct TrustedData {
        uint64_t times = 0;
        uint64_t times_trusted = 0;
        csdb::Amount totalFee;
    };

    void convert(const csdb::Address& address, WalletData::Address& walletAddress) const;
    static void convert(const WalletData::Address& walletAddress, csdb::Address& address);

    void iterateOverWallets(const std::function<bool(const WalletData::Address&, const WalletData&)>);

#ifdef MONITOR_NODE
    void iterateOverWriters(const std::function<bool(const WalletData::Address&, const TrustedData&)>);
#endif

    uint64_t getCount() const {
        return wallets_.size();
    }

private:
    const Config config_;
    WalletsIds& walletsIds_;
    const csdb::Address genesisAddress_;
    const csdb::Address startAddress_;
    std::list<csdb::TransactionID> smartPayableTransactions_;
    std::map< csdb::Address, std::list<RefContractCall> > canceledSmarts_;

#ifdef MONITOR_NODE
    std::map<WalletData::Address, TrustedData> trusted_info_;
#endif

    std::vector<WalletData*> wallets_;
};

class WalletsCache::Updater {
public:
    Updater(WalletsCache& data);

    void loadNextBlock(csdb::Pool& curr, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain);

    const WalletData* findWallet(WalletId id) const;
    const Mask& getModified() const { return modified_; }

    void invokeReplenishPayableContract(const csdb::Transaction&);
    void rollbackExceededTimeoutContract(const csdb::Transaction&, const WalletsCache::RefContractCall&, const csdb::Amount& execFee = 0);
    void smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx, const csdb::Transaction& initTrx);

private:
    bool findWalletId(const csdb::Address& address, WalletId& id);
    WalletData& getWalletData(WalletId id, const csdb::Address& address);
    void setModified(WalletId id);

    double load(const csdb::Transaction& tr, const BlockChain& blockchain);
    double loadTrxForSource(const csdb::Transaction& tr, const BlockChain& blockchain);
    void loadTrxForTarget(const csdb::Transaction& tr);

    void fundConfidantsWalletsWithFee(const csdb::Amount& totalFee, const cs::ConfidantsKeys& confidants, const std::vector<uint8_t>& realTrusted);
    void fundConfidantsWalletsWithExecFee(const csdb::Transaction& transaction, const BlockChain& blockchain);

    void checkSmartWaitingForMoney(const csdb::Transaction& initTransaction, const WalletsCache::RefContractCall& initRef, const csdb::Transaction& newStateTransaction);
    bool isCanceledSmart(const csdb::Address& contract_addr, const WalletsCache::RefContractCall& ref);
    void checkCanceledSmart(const csdb::Address& contract_addr, const WalletsCache::RefContractCall& ref);

#ifdef MONITOR_NODE
    bool setWalletTime(const WalletData::Address& address, const uint64_t& p_timeStamp);
#endif

    Mask modified_;
    WalletsCache& data_;
};
} // namespace cs
#endif // WALLETS_CACHE_HPP
