#ifndef WALLETS_CACHE_HPP
#define WALLETS_CACHE_HPP

#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <boost/dynamic_bitset.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/transactionstail.hpp>
#include <cscrypto/cscrypto.hpp>
#include <memory>
#include <vector>
#include <map>
#include <list>

#include <lib/system/common.hpp>

class BlockChain;

namespace csdb {
class Pool;
class Transaction;
}  // namespace csdb

namespace cs {
class WalletsIds;

class WalletsCache {
public:
  using WalletId = csdb::internal::WalletId;
  using Mask = boost::dynamic_bitset<uint64_t>;

  struct Config {
    size_t initialWalletsNum_ = 2 * 1024 * 1024;
  };

public:
  struct WalletData {
    using Address = cs::PublicKey;

    Address address_;
    csdb::Amount balance_;
    TransactionsTail trxTail_;

#ifdef MONITOR_NODE
    uint64_t createTime_ = 0;
    uint64_t transNum_ = 0;
#endif
#ifdef TRANSACTIONS_INDEX
    csdb::TransactionID lastTransaction_;
#endif
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

  uint64_t getCount() const { return wallets_.size(); }

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
    void fundConfidantsWalletsWithFee(double totalFee, const cs::ConfidantsKeys& confidants,
                                      const std::vector<uint8_t>& realTrusted);
    void loadTrxForTarget(const csdb::Transaction& tr);
    virtual WalletData& getWalletData(WalletId id, const csdb::Address& address) = 0;
    virtual void setModified(WalletId id) = 0;
    void invokeReplenishPayableContract(const csdb::Transaction&);
    void rollbackReplenishPayableContract(const csdb::Transaction&);
    void smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx,
                                        const csdb::Transaction& initTrx);
    void checkSmartWaitingForMoney(const csdb::Transaction& initTransaction, const csdb::Transaction& newStateTransaction);
    bool isClosedSmart(const csdb::Transaction& transaction);
    void checkClosedSmart(const csdb::Transaction& transaction);
    void fundConfidantsWalletsWithExecFee(const csdb::Transaction& transaction, const BlockChain& blockchain);

/*#ifdef MONITOR_NODE
    std::map<WalletData::Address, WriterData> writers_;
#endif*/

  protected:
    static WalletData& getWalletData(Data& wallets, WalletId id, const csdb::Address& address);
#ifdef MONITOR_NODE
    bool setWalletTime(const WalletData::Address& address, const uint64_t &p_timeStamp);
#endif

  protected:
    WalletsCache& data_;
  };

public:
  class Initer : protected ProcessorBase {
  public:
    using ProcessorBase::invokeReplenishPayableContract;
    using ProcessorBase::rollbackReplenishPayableContract;
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
    using ProcessorBase::rollbackReplenishPayableContract;
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

  static csdb::Address findSmartContractIniter(const csdb::Transaction& tr, const BlockChain& blockchain);
  static csdb::Transaction findSmartContractInitTrx(const csdb::Transaction& tr, const BlockChain& blockchain);

  std::unique_ptr<Initer> createIniter();
  std::unique_ptr<Updater> createUpdater();

private:
  const Config config_;
  WalletsIds& walletsIds_;
  const csdb::Address genesisAddress_;
  const csdb::Address startAddress_;
  std::list<csdb::Transaction> smartPayableTransactions_;
  std::list<csdb::Transaction> closedSmarts_;

#ifdef MONITOR_NODE
  std::map<WalletData::Address, TrustedData> trusted_info_;
#endif

  Data wallets_;
};

}  // namespace cs

#endif
