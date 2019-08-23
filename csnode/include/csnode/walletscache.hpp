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

    struct RefContractCall {
        cs::Sequence sequence;
        uint32_t transaction;
    };

    struct Config {
        size_t initialWalletsNum_ = InitialWalletsNum;
    };

public:
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

public:
    static void convert(const csdb::Address& address, WalletData::Address& walletAddress);
    static void convert(const WalletData::Address& walletAddress, csdb::Address& address);

    void iterateOverWallets(const std::function<bool(const WalletData::Address&, const WalletData&)>);

#ifdef MONITOR_NODE
    void iterateOverWriters(const std::function<bool(const WalletData::Address&, const TrustedData&)>);
#endif

    uint64_t getCount() const {
        return wallets_.size();
    }

private:
    using Data = std::vector<WalletData*>;

    class ProcessorBase {
    public:
        ProcessorBase(WalletsCache& data)
        : data_(data) {
        }
        virtual ~ProcessorBase() {
        }
        virtual bool findWalletId(const csdb::Address& address, WalletId& id) = 0;

    protected:
        void load(csdb::Pool& curr, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain);
        double load(const csdb::Transaction& tr, const BlockChain& blockchain);
        double loadTrxForSource(const csdb::Transaction& tr, const BlockChain& blockchain);
        void fundConfidantsWalletsWithFee(const csdb::Amount& totalFee, const cs::ConfidantsKeys& confidants, const std::vector<uint8_t>& realTrusted);
        void loadTrxForTarget(const csdb::Transaction& tr);
        virtual WalletData& getWalletData(WalletId id, const csdb::Address& address) = 0;
        virtual void setModified(WalletId id) = 0;
        void invokeReplenishPayableContract(const csdb::Transaction&);
        void rollbackExceededTimeoutContract(const csdb::Transaction&, const WalletsCache::RefContractCall&, const csdb::Amount& execFee = 0);
        void smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx, const csdb::Transaction& initTrx);
        void checkSmartWaitingForMoney(const csdb::Transaction& initTransaction, const WalletsCache::RefContractCall& initRef, const csdb::Transaction& newStateTransaction);
        bool isCanceledSmart(const csdb::Address& contract_addr, const WalletsCache::RefContractCall& ref);
        void checkCanceledSmart(const csdb::Address& contract_addr, const WalletsCache::RefContractCall& ref);
        void fundConfidantsWalletsWithExecFee(const csdb::Transaction& transaction, const BlockChain& blockchain);

        /*#ifdef MONITOR_NODE
            std::map<WalletData::Address, WriterData> writers_;
        #endif*/

    protected:
        static WalletData& getWalletData(Data& wallets, WalletId id, const csdb::Address& address);
#ifdef MONITOR_NODE
        bool setWalletTime(const WalletData::Address& address, const uint64_t& p_timeStamp);
#endif

    protected:
        WalletsCache& data_;
    };

public:
    class Initer : protected ProcessorBase {
    public:
        using ProcessorBase::invokeReplenishPayableContract;
        using ProcessorBase::rollbackExceededTimeoutContract;
        Initer(WalletsCache& data);
        void loadPrevBlock(csdb::Pool& curr, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain);
        bool moveData(WalletId srcIdSpecial, WalletId destIdNormal);
        bool isFinishedOk() const;

    protected:
        bool findWalletId(const csdb::Address& address, WalletId& id) override;
        WalletData& getWalletData(WalletId id, const csdb::Address& address) override;
        void setModified(WalletId id) override;

    protected:
        Data walletsSpecial_;
    };

    class Updater : protected ProcessorBase {
    public:
        using ProcessorBase::invokeReplenishPayableContract;
        using ProcessorBase::rollbackExceededTimeoutContract;
        using ProcessorBase::smartSourceTransactionReleased;
        Updater(WalletsCache& data);
        void loadNextBlock(csdb::Pool& curr, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain);
        const WalletData* findWallet(WalletId id) const;
        const Mask& getModified() const {
            return modified_;
        }

    protected:
        bool findWalletId(const csdb::Address& address, WalletId& id) override;
        WalletData& getWalletData(WalletId id, const csdb::Address& address) override;
        void setModified(WalletId id) override;

    protected:
        Mask modified_;
    };

public:
    WalletsCache(const Config& config, csdb::Address genesisAddress, csdb::Address startAddress, WalletsIds& walletsIds);
    ~WalletsCache();
    WalletsCache(const WalletsCache&) = delete;
    WalletsCache& operator=(const WalletsCache&) = delete;
    WalletsCache(const WalletsCache&&) = delete;
    WalletsCache& operator=(const WalletsCache&&) = delete;

    std::unique_ptr<Initer> createIniter();
    std::unique_ptr<Updater> createUpdater();

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

    Data wallets_;
};

}  // namespace cs

#endif
