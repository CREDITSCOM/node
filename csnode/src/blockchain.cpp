#include <base58.h>
#include <csdb/currency.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <limits>

#include <csnode/blockchain.hpp>
#include <csnode/blockhashes.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>
#include <csnode/fee.hpp>
#include <csnode/nodeutils.hpp>
#include <csnode/transactionsiterator.hpp>
#include <solver/smartcontracts.hpp>

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>

#include <client/config.hpp>

using namespace cs;
namespace fs = boost::filesystem;

namespace {
const char* cachesPath = "./caches";
const std::string lastIndexedPath = std::string(cachesPath) + "/last_indexed";
cs::Sequence lastIndexedPool;

using FileSource = boost::iostreams::mapped_file_source;
using FileSink = boost::iostreams::mapped_file_sink;

template <class BoostMMapedFile>
class MMappedFileWrap {
public:
    MMappedFileWrap(const std::string& path,
            size_t maxSize = boost::iostreams::mapped_file::max_length,
            bool createNew = true) {
        try {
            if (!createNew) {
                file_.open(path, maxSize);
            }
            else {
                boost::iostreams::mapped_file_params params;
                params.path = path;
                params.new_file_size = maxSize;
                file_.open(params);
            }
        }
        catch (std::exception& e) {
            cserror() << e.what();
        }
        catch (...) {
            cserror() << __FILE__ << ", "
                      << __LINE__
                      << " exception ...";
        }
    }

    bool isOpen() {
        return file_.is_open();
    }

    ~MMappedFileWrap() {
        if (isOpen()) {
            file_.close();
        }
    }

    template<typename T>
    T* data() {
        return isOpen() ? (T*)file_.data() : nullptr;
    }

private:
    BoostMMapedFile file_;
};

inline void checkLastIndFile(bool& recreateIndex) {
    fs::path p(lastIndexedPath);
    if (!fs::is_regular_file(p)) {
        recreateIndex = true;
        return;
    }
    MMappedFileWrap<FileSource> f(lastIndexedPath, sizeof(cs::Sequence), false);
    if (!f.isOpen()) {
        recreateIndex = true; 
        return;
    }
    lastIndexedPool = *(f.data<const cs::Sequence>());
}

inline void updateLastIndFile() {
    static MMappedFileWrap<FileSink> f(lastIndexedPath, sizeof(cs::Sequence));
    auto ptr = f.data<cs::Sequence>();
    if (ptr) {
        *ptr = lastIndexedPool;
    }
}
} // namespace

BlockChain::BlockChain(csdb::Address genesisAddress, csdb::Address startAddress, bool recreateIndex)
: good_(false)
, dbLock_()
, genesisAddress_(genesisAddress)
, startAddress_(startAddress)
, walletIds_(new WalletsIds)
, walletsCacheStorage_(new WalletsCache(*walletIds_))
, walletsPools_(new WalletsPools(genesisAddress, startAddress, *walletIds_))
, cacheMutex_()
, recreateIndex_(recreateIndex) {
    cs::Connector::connect(&storage_.readBlockEvent(), this, &BlockChain::onReadFromDB);

    createCachesPath();
    if (!recreateIndex_) {
        checkLastIndFile(recreateIndex_);
    }

    walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();
    blockHashes_ = std::make_unique<cs::BlockHashes>(cachesPath);
}

BlockChain::~BlockChain() {
}

bool BlockChain::init(const std::string& path) {
    cslog() << "Trying to open DB...";

    size_t totalLoaded = 0;
    lastSequence_ = 0;
    csdb::Storage::OpenCallback progress = [&](const csdb::Storage::OpenProgress& progress) {
        ++totalLoaded;
        if (progress.poolsProcessed % 1000 == 0) {
            std::cout << '\r' << WithDelimiters(progress.poolsProcessed) << std::flush;
        }
        return false;
    };

    if (!storage_.open(path, progress)) {
        cserror() << "Couldn't open database at " << path;
        return false;
    }

    cslog() << "\rDB is opened, loaded " << WithDelimiters(totalLoaded) << " blocks";

    if (storage_.last_hash().is_empty()) {
        csdebug() << "Last hash is empty...";
        if (storage_.size()) {
            cserror() << "failed!!! Delete the Database!!! It will be restored from nothing...";
            return false;
        }
        writeGenesisBlock();
    }
    else {
        if (!postInitFromDB()) {
            return false;
        }
    }

    if (recreateIndex_) {
        recreateIndex_ = false;
        lapoos.clear();
        cslog() << "Recreated index 0 -> " << getLastSeq()
                << ". Continue to keep it actual from new blocks.";
    }

    good_ = true;
    return true;
}

bool BlockChain::isGood() const {
    return good_;
}

uint64_t BlockChain::uuid() const {
    cs::Lock lock(dbLock_);
    return uuid_;
}

void BlockChain::onReadFromDB(csdb::Pool block, bool* shouldStop) {
    auto blockSeq = block.sequence();
    lastSequence_ = blockSeq;
    if (blockSeq == 0 && recreateIndex_) {
      cs::Lock lock(dbLock_);
      storage_.truncate_trxs_index();
    }
    if (blockSeq == 1) {
      cs::Lock lock(dbLock_);
      uuid_ = uuidFromBlock(block);
      csdebug() << "Blockchain: UUID = " << uuid_;
    }

    if (!updateWalletIds(block, *walletsCacheUpdater_.get())) {
        cserror() << "Blockchain: updateWalletIds() failed on block #" << block.sequence();
        *shouldStop = true;
    }
    else {
        if (!blockHashes_->onNextBlock(block)) {
            cserror() << "Blockchain: blockHashes_->onReadBlock(block) failed on block #" << block.sequence();
            *shouldStop = true;
        }
        else {
            if (recreateIndex_ || lastIndexedPool < block.sequence()) {
                createTransactionsIndex(block);
            }
            updateNonEmptyBlocks(block);
        }
        walletsCacheUpdater_->loadNextBlock(block, block.confidants(), *this);
    }
}

inline void BlockChain::updateNonEmptyBlocks(const csdb::Pool& pool) {
    const auto cntTr = pool.transactions_count();
    if (cntTr > 0) {
        total_transactions_count_ += cntTr;

        if (lastNonEmptyBlock_.transCount && pool.sequence() != lastNonEmptyBlock_.poolSeq) {
            previousNonEmpty_[pool.sequence()] = lastNonEmptyBlock_;
        }
        lastNonEmptyBlock_.poolSeq = pool.sequence();
        lastNonEmptyBlock_.transCount = static_cast<uint32_t>(cntTr);
    }
}

bool BlockChain::postInitFromDB() {
    auto func = [](const cs::PublicKey& key, const WalletData& wallet) {
        double bal = wallet.balance_.to_double();
        if (bal < -std::numeric_limits<double>::min()) {
            csdebug() << "Wallet with negative balance (" << bal << ") detected: "
                      << cs::Utils::byteStreamToHex(key.data(), key.size()) << " ("
                      << EncodeBase58(key.data(), key.data() + key.size()) << ")";
        }
        return true;
    };
    walletsCacheStorage_->iterateOverWallets(func);
    return true;
}

void BlockChain::createTransactionsIndex(csdb::Pool& pool) {
    std::set<csdb::Address> indexedAddrs;

    auto lbd = [&indexedAddrs, &pool, this](const csdb::Address& addr) {
        auto key = getAddressByType(addr, BlockChain::AddressType::PublicKey);
        if (indexedAddrs.insert(key).second) {
            cs::Sequence lapoo;
            if (recreateIndex_) {
                lapoo = lapoos[key];
                lapoos[key] = pool.sequence();
            }
            else {
                lapoo = getLastTransaction(key).pool_seq();
            }
            std::lock_guard<decltype(dbLock_)> l(dbLock_);
            if (!storage_.set_previous_transaction_block(key, pool.sequence(), lapoo)) {
            // errors in database, or handler has been deleted
                csdebug() << "Create trx index: can't set_previous_transaction_block"
                          << " on pool sequence " << pool.sequence();
                return false;
            }
        }
        return true;
    };

    for (auto& tr : pool.transactions()) {
        if (!lbd(tr.source())) return;
        if (!lbd(tr.target())) return;
    }

    lastIndexedPool = pool.sequence();
    updateLastIndFile();
}

csdb::PoolHash BlockChain::getLastHash() const {
    std::lock_guard lock(dbLock_);

    if (deferredBlock_.is_valid()) {
        return deferredBlock_.hash().clone();
    }

    return storage_.last_hash();
}

std::string BlockChain::getLastTimeStamp() const {
    std::lock_guard<decltype(dbLock_)> l(dbLock_);

    if (deferredBlock_.is_valid()) {
        return deferredBlock_.user_field(0).value<std::string>();
    }
    else {
        return getLastBlock().user_field(0).value<std::string>();
    }
}

cs::Bytes BlockChain::getLastRealTrusted() const {
    std::lock_guard<decltype(dbLock_)> l(dbLock_);

    if (deferredBlock_.is_valid()) {
        return cs::Utils::bitsToMask(deferredBlock_.numberTrusted(), deferredBlock_.realTrusted());
    }
    else {
        return cs::Utils::bitsToMask(getLastBlock().numberTrusted(), getLastBlock().realTrusted());
    }
}

void BlockChain::writeGenesisBlock() {
    cswarning() << "Adding the genesis block";

    csdb::Pool genesis;
    csdb::Transaction transaction;

    std::string strAddr = "5B3YXqDTcWQFGAqEJQJP3Bg1ZK8FFtHtgCiFLT5VAxpe";
    std::vector<uint8_t> pub_key;
    DecodeBase58(strAddr, pub_key);

    csdb::Address test_address = csdb::Address::from_public_key(pub_key);
    transaction.set_target(test_address);
    transaction.set_source(genesisAddress_);
    transaction.set_currency(csdb::Currency(1));
    transaction.set_amount(csdb::Amount(249'471'071, 0));
    transaction.set_max_fee(csdb::AmountCommission(0.0));
    transaction.set_counted_fee(csdb::AmountCommission(0.0));
    transaction.set_innerID(0);

    genesis.add_transaction(transaction);

    genesis.set_previous_hash(csdb::PoolHash());
    genesis.set_sequence(0);
    addNewWalletsToPool(genesis);

    csdebug() << "Genesis block completed ... trying to save";

    finalizeBlock(genesis, true, cs::PublicKeys{});
    deferredBlock_ = genesis;
    emit storeBlockEvent(deferredBlock_);

    csdebug() << genesis.hash().to_string();

    uint32_t bSize;
    genesis.to_byte_stream(bSize);
}

void BlockChain::iterateOverWallets(const std::function<bool(const cs::PublicKey&, const cs::WalletsCache::WalletData&)> func) {
    std::lock_guard lock(cacheMutex_);
    walletsCacheStorage_->iterateOverWallets(func);
}

#ifdef MONITOR_NODE
void BlockChain::iterateOverWriters(const std::function<bool(const cs::PublicKey&, const cs::WalletsCache::TrustedData&)> func) {
    std::lock_guard lock(cacheMutex_);
    walletsCacheStorage_->iterateOverWriters(func);
}

void BlockChain::applyToWallet(const csdb::Address& addr, const std::function<void(const cs::WalletsCache::WalletData&)> func) {
    std::lock_guard lock(cacheMutex_);
    auto pub = getAddressByType(addr, BlockChain::AddressType::PublicKey);
    auto wd = walletsCacheUpdater_->findWallet(pub.public_key());

    func(*wd);
}
#endif

size_t BlockChain::getSize() const {
    std::lock_guard lock(dbLock_);
    const auto storageSize = storage_.size();
    return deferredBlock_.is_valid() ? (storageSize + 1) : storageSize;
}

csdb::Pool BlockChain::loadBlock(const csdb::PoolHash& ph) const {
    if (ph.is_empty()) {
        return csdb::Pool{};
    }

    std::lock_guard l(dbLock_);

    if (deferredBlock_.hash() == ph) {
        return deferredBlock_.clone();
    }

    return storage_.pool_load(ph);
}

csdb::Pool BlockChain::loadBlock(const cs::Sequence sequence) const {
    std::lock_guard lock(dbLock_);

    if (deferredBlock_.is_valid() && deferredBlock_.sequence() == sequence) {
        // deferredBlock already composed:
        return deferredBlock_.clone();
    }
    if (sequence > getLastSeq()) {
        return csdb::Pool{};
    }
    return storage_.pool_load(sequence);
}

csdb::Pool BlockChain::loadBlockMeta(const csdb::PoolHash& ph, size_t& cnt) const {
    std::lock_guard lock(dbLock_);

    if (deferredBlock_.hash() == ph) {
        return deferredBlock_.clone();
    }

    return storage_.pool_load_meta(ph, cnt);
}

csdb::Transaction BlockChain::loadTransaction(const csdb::TransactionID& transId) const {
    std::lock_guard l(dbLock_);
    csdb::Transaction transaction;

    if (deferredBlock_.sequence() == transId.pool_seq()) {
        transaction = deferredBlock_.transaction(transId).clone();
        transaction.set_time(deferredBlock_.get_time());
    }
    else {
        transaction = storage_.transaction(transId);
        transaction.set_time(storage_.pool_load(transId.pool_seq()).get_time());
    }

    return transaction;
}

void BlockChain::removeLastBlock() {
    csmeta(csdebug) << "begin";
    csdb::Pool pool{};

    {
        std::lock_guard lock(dbLock_);

        if (deferredBlock_.is_valid()) {
            pool = deferredBlock_;
            deferredBlock_ = csdb::Pool{};
        }
        else {
            pool = storage_.pool_remove_last();
        }
    }

    if (!pool.is_valid()) {
        csmeta(cserror) << "Error! Removed pool is not valid";
        return;
    }

    if (pool.sequence() == 0) {
        csmeta(cswarning) << "Attempt to remove Genesis block !!!!!";
        return;
    }

    // to be sure, try to remove both sequence and hash
    if (!blockHashes_->remove(pool.sequence())) {
        blockHashes_->remove(pool.hash());
    }
	--lastSequence_;
    total_transactions_count_ -= pool.transactions().size();
    walletsCacheUpdater_->loadNextBlock(pool, pool.confidants(), *this, true);
    removeWalletsInPoolFromCache(pool);
    removeLastBlockFromTrxIndex(pool);

    emit removeBlockEvent(pool.sequence());

    csmeta(csdebug) << "done";
}

csdb::Address BlockChain::getAddressFromKey(const std::string& key) {
    if (key.size() == kPublicKeyLength) {
        csdb::Address res = csdb::Address::from_public_key(key.data());
        return res;
    }
    else {
        csdb::internal::WalletId id = *reinterpret_cast<const csdb::internal::WalletId*>(key.data());
        csdb::Address res = csdb::Address::from_wallet_id(id);
        return res;
    }
}

void BlockChain::removeLastBlockFromTrxIndex(const csdb::Pool& pool) {
    std::set<csdb::Address> uniqueAddresses;
    std::vector<std::pair<cs::PublicKey, csdb::TransactionID>> updates;

    auto lbd = [&updates, &uniqueAddresses, this](const csdb::Address& addr, cs::Sequence sq) {
        auto key = getAddressByType(addr, AddressType::PublicKey);

        if (uniqueAddresses.insert(key).second) {
            auto it = cs::TransactionsIterator(*this, addr);
            bool found = false;

            for (; it.isValid(); it.next()) {
                if (it->id().pool_seq() < sq) {
                    updates.push_back(std::make_pair(key.public_key(), it->id()));
                    found = true;
                    break;
                }
            }
            if (!found) {
                updates.push_back(std::make_pair(key.public_key(),
                                                 csdb::TransactionID(kWrongSequence, kWrongSequence)));
            }

            std::lock_guard<decltype(dbLock_)> l(dbLock_);
            storage_.remove_last_from_trx_index(key, sq);
        }
    };

    for (const auto& t : pool.transactions()) {
        lbd(t.source(), lastIndexedPool);
        lbd(t.target(), lastIndexedPool);
    }
    --lastIndexedPool;
    updateLastIndFile();

    if (lastNonEmptyBlock_.poolSeq == pool.sequence()) {
        lastNonEmptyBlock_ = previousNonEmpty_[lastNonEmptyBlock_.poolSeq];
        previousNonEmpty_.erase(pool.sequence());
    }

    if (updates.size()) {
        std::lock_guard l(cacheMutex_);
        walletsCacheUpdater_->updateLastTransactions(updates);
    }
}

void BlockChain::removeWalletsInPoolFromCache(const csdb::Pool& pool) {
    try {
        std::lock_guard lock(cacheMutex_);
        const csdb::Pool::NewWallets& newWallets = pool.newWallets();

        for (const auto& newWall : newWallets) {
            csdb::Address newWallAddress;
            if (!pool.getWalletAddress(newWall, newWallAddress)) {
                cserror() << "Wrong new wallet data";
                return;
            }
            if (!walletIds_->normal().remove(newWallAddress)) {
                cswarning() << "Wallet was not removed";
            }
        }
    }
    catch (std::exception& e) {
        cserror() << "Exc=" << e.what();
    }
    catch (...) {
        cserror() << "Exc=...";
    }
}

void BlockChain::logBlockInfo(csdb::Pool& pool) {
    const auto& trusted = pool.confidants();
    std::string realTrustedString;
    auto mask = cs::Utils::bitsToMask(pool.numberTrusted(), pool.realTrusted());
    for (auto i : mask) {
        realTrustedString = realTrustedString + "[" + std::to_string(static_cast<int>(i)) + "] ";
    }

    csdebug() << " trusted count " << trusted.size() << ", RealTrusted = " << realTrustedString;
    for (const auto& t : trusted) {
        csdebug() << "\t- " << cs::Utils::byteStreamToHex(t.data(), t.size());
    }
    csdebug() << " transactions count " << pool.transactions_count();
    if (pool.user_field_ids().count(0) > 0) {
        csdebug() << " time: " << pool.user_field(0).value<std::string>().c_str();
    }
    csdebug() << " previous hash: " << pool.previous_hash().to_string();
    csdebug() << " hash(" << pool.sequence() << "): " << pool.hash().to_string();
    csdebug() << " last storage size: " << getSize();
}

bool BlockChain::finalizeBlock(csdb::Pool& pool, bool isTrusted, cs::PublicKeys lastConfidants) {
    if (!pool.compose()) {
        csmeta(cserror) << "Couldn't compose block: " << pool.sequence();
        return false;
    }

    cs::Sequence currentSequence = pool.sequence();
    const auto& confidants = pool.confidants();
    const auto& signatures = pool.signatures();
    const auto& realTrusted = pool.realTrusted();
    if (currentSequence > 1) {
        csdebug() << "Finalize: starting confidants validation procedure:";

        cs::Bytes trustedToHash;
        cs::DataStream tth(trustedToHash);
        tth << currentSequence;
        tth << confidants;

        cs::Hash trustedHash = cscrypto::calculateHash(trustedToHash.data(), trustedToHash.size());

        cs::Signatures sigs = pool.roundConfirmations();
        const auto& confMask = cs::Utils::bitsToMask(pool.numberConfirmations(), pool.roundConfirmationMask());
        // for debugging only delete->
        csdebug() << "Mask size = " << confMask.size() << " for next confidants:";
        for (auto& it : lastConfidants) {
            csdebug() << cs::Utils::byteStreamToHex(it.data(), it.size());
        }
        // <-delete
        if (confMask.size() > 1) {
            if (!NodeUtils::checkGroupSignature(lastConfidants, confMask, sigs, trustedHash)) {
                csdebug() << "           The Confidants confirmations are not OK";
                return false;
            }
            else {
                csdebug() << "           The Confidants confirmations are OK";
            }
        }
        else {
            // TODO: add SS PKey to the prevConfidants
        }
    }

    if (signatures.empty() && (!isTrusted || pool.sequence() != 0)) {
        csmeta(csdebug) << "The pool #" << pool.sequence() << " doesn't contain signatures";
        return false;
    }

    if (signatures.size() < static_cast<size_t>(cs::Utils::maskValue(realTrusted)) && !isTrusted && pool.sequence() != 0) {
        csmeta(csdebug) << "The number of signatures is insufficient";
        return false;
    }
    auto mask = cs::Utils::bitsToMask(pool.numberTrusted(), pool.realTrusted());

    // pool signatures check: start
    if (pool.sequence() > 0) {
        //  csmeta(csdebug) << "Pool Hash: " << cs::Utils::byteStreamToHex(pool.hash().to_binary().data(), pool.hash().to_binary().size());
        //  csmeta(csdebug) << "Prev Hash: " << cs::Utils::byteStreamToHex(pool.previous_hash().to_binary().data(), pool.previous_hash().to_binary().size());
        Hash tempHash;
        auto hash = pool.hash().to_binary();
        std::copy(hash.cbegin(), hash.cend(), tempHash.data());
        if (NodeUtils::checkGroupSignature(confidants, mask, signatures, tempHash)) {
            csmeta(csdebug) << "The number of signatures is sufficient and all of them are OK!";
        }
        else {
            cswarning() << "Some of Pool Signatures aren't valid. The pool will not be written to DB. It will be automatically written, when we get proper data";
            return false;
        }
    }
    else {
        csmeta(csdebug) << "Genesis block will be written without signatures verification";
    }
    // pool signatures check: end

    createTransactionsIndex(pool);
    updateNonEmptyBlocks(pool);

    if (!updateFromNextBlock(pool)) {
        csmeta(cserror) << "Error in updateFromNextBlock()";
        return false;
    }

    csmeta(csdetails) << "last hash: " << pool.hash().to_string();
    return true;
}

csdb::PoolHash BlockChain::getHashBySequence(cs::Sequence seq) const {
    std::lock_guard lock(dbLock_);

    if (deferredBlock_.sequence() == seq) {
        return deferredBlock_.hash().clone();
    }

    csdb::PoolHash tmp = blockHashes_->find(seq);
    if (!tmp.is_empty()) {
        return tmp;
    }

    return storage_.pool_hash(seq);
}

cs::Sequence BlockChain::getSequenceByHash(const csdb::PoolHash& hash) const {
    std::lock_guard lock(dbLock_);
    
    if (deferredBlock_.hash() == hash) {
        return deferredBlock_.sequence();
    }

    cs::Sequence seq = blockHashes_->find(hash);
    if (seq != kWrongSequence) {
        return seq;
    }

    return storage_.pool_sequence(hash);
}

uint64_t BlockChain::getWalletsCountWithBalance() {
    std::lock_guard lock(cacheMutex_);

    uint64_t count = 0;
    auto proc = [&](const cs::PublicKey&, const WalletData& wallet) {
        constexpr csdb::Amount zero_balance(0);
        if (wallet.balance_ >= zero_balance) {
            count++;
        }
        return true;
    };
    walletsCacheStorage_->iterateOverWallets(proc);
    return count;
}

class BlockChain::TransactionsLoader {
public:
    using Transactions = std::vector<csdb::Transaction>;

public:
    TransactionsLoader(csdb::Address wallPubKey, BlockChain::WalletId id, bool isToLoadWalletsPoolsCache, BlockChain& blockchain, Transactions& transactions)
    : wallPubKey_(wallPubKey)
    , isToLoadWalletsPoolsCache_(isToLoadWalletsPoolsCache)
    , blockchain_(blockchain)
    , transactions_(transactions) {
        if (isToLoadWalletsPoolsCache_) {
            std::lock_guard lock(blockchain_.cacheMutex_);
            blockchain.walletsPools_->addWallet(id);
        }
    }

    bool load(const csdb::PoolHash& poolHash, uint64_t& offset, uint64_t limit, csdb::PoolHash& prevPoolHash) {
        csdb::Pool curr = blockchain_.loadBlock(poolHash);
        if (!curr.is_valid())
            return false;

        if (curr.transactions_count()) {
            bool hasMyTransactions = false;

            for (auto trans : curr.transactions()) {
                if (transactions_.size() == limit)
                    break;

                if (trans.target() == wallPubKey_ || trans.source() == wallPubKey_) {
                    hasMyTransactions = true;

                    if (offset == 0)
                        transactions_.push_back(trans);
                    else
                        --offset;
                }
            }

            if (hasMyTransactions && isToLoadWalletsPoolsCache_) {
                std::lock_guard lock(blockchain_.cacheMutex_);
                blockchain_.walletsPools_->loadPrevBlock(curr);
            }
        }

        prevPoolHash = curr.previous_hash();

        return true;
    }

private:
    csdb::Address wallPubKey_;
    const bool isToLoadWalletsPoolsCache_;
    BlockChain& blockchain_;
    Transactions& transactions_;
};

void BlockChain::getTransactions(Transactions& transactions, csdb::Address address, uint64_t offset, uint64_t limit) {
    for (auto trIt = cs::TransactionsIterator(*this, address); trIt.isValid(); trIt.next()) {
        if (offset > 0) {
            --offset;
            continue;
        }

        transactions.push_back(*trIt);
        transactions.back().set_time(trIt.getPool().get_time());

        if (--limit == 0)
            break;
    }
}

bool BlockChain::findDataForTransactions(csdb::Address address, csdb::Address& wallPubKey, WalletId& id, WalletsPools::WalletData::PoolsHashes& hashesArray) const {
    std::lock_guard lock(cacheMutex_);

    if (address.is_wallet_id()) {
        id = address.wallet_id();

        auto pubKey = getAddressByType(address, AddressType::PublicKey);
        const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(pubKey.public_key());

        if (!wallDataPtr) {
            return false;
        }

        wallPubKey = pubKey;
    }
    else
    {
        if (!walletIds_->normal().find(address, id)) {
            return false;
        }

        wallPubKey = address;
    }

    const WalletsPools::WalletData* wallData = walletsPools_->findWallet(id);
    if (wallData) {
        hashesArray = wallData->poolsHashes_;
    }

    return true;
}

void BlockChain::getTransactions(Transactions& transactions, csdb::Address wallPubKey, WalletId id, const WalletsPools::WalletData::PoolsHashes& hashesArray, uint64_t offset,
                                 uint64_t limit) {
    bool isToLoadWalletsPoolsCache = hashesArray.empty() && wallPubKey != genesisAddress_ && wallPubKey != startAddress_;
    if (wallPubKey.is_public_key()) {
        WalletId _id;

        if (!findWalletId(wallPubKey, _id)) {
            return;
        }

        wallPubKey = csdb::Address::from_wallet_id(_id);
    }

    TransactionsLoader trxLoader(wallPubKey, id, isToLoadWalletsPoolsCache, *this, transactions);
    csdb::PoolHash prevHash = getLastHash();

    for (size_t i = hashesArray.size() - 1; i != std::numeric_limits<decltype(i)>::max(); --i) {
        const auto& poolHashData = hashesArray[i];

        if (poolHashData.trxNum < WalletsPools::WalletData::PoolHashData::maxTrxNum && poolHashData.trxNum <= offset) {
            offset -= poolHashData.trxNum;
            continue;
        }

        csdb::PoolHash currHash;
        WalletsPools::convert(poolHashData.poolHash, currHash);

        if (!trxLoader.load(currHash, offset, limit, prevHash)) {
            return;
        }

        if (transactions.size() >= limit) {
            return;
        }
    }

    while (true) {
        csdb::PoolHash currHash = prevHash;

        if (!trxLoader.load(currHash, offset, limit, prevHash)) {
            break;
        }
    }
}

bool BlockChain::updateWalletIds(const csdb::Pool& pool, WalletsCache::Updater& proc) {
    try {
        std::lock_guard lock(cacheMutex_);

        const csdb::Pool::NewWallets& newWallets = pool.newWallets();
        for (const auto& newWall : newWallets) {
            csdb::Address newWallAddress;
            if (!pool.getWalletAddress(newWall, newWallAddress)) {
                cserror() << "Wrong new wallet data";
                return false;
            }

            if (!insertNewWalletId(newWallAddress, newWall.walletId_, proc)) {
                cserror() << "Wallet was already added as new";
            }
        }
    }
    catch (std::exception& e) {
        cserror() << "Exc=" << e.what();
        return false;
    }
    catch (...) {
        cserror() << "Exc=...";
        return false;
    }

    return true;
}

bool BlockChain::insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, WalletsCache::Updater&) {
    if (!walletIds_->normal().insert(newWallAddress, newWalletId)) {
        cserror() << "Cannot add new wallet";
        return false;
    }

    return true;
}

void BlockChain::addNewWalletToPool(const csdb::Address& walletAddress, const csdb::Pool::NewWalletInfo::AddressId& addressId, csdb::Pool::NewWallets& newWallets) {
    if (!walletAddress.is_public_key()) {
        return;
    }

    if (walletAddress == genesisAddress_) {
        return;
    }

    WalletId id{};

    if (getWalletId(walletAddress, id)) {
        newWallets.emplace_back(csdb::Pool::NewWalletInfo{addressId, id});
    }
}

void BlockChain::addNewWalletsToPool(csdb::Pool& pool) {
    csdb::Pool::NewWallets* newWallets = pool.newWallets();

    if (!newWallets) {
        cserror() << "Pool is read-only";
        return;
    }

    newWallets->clear();

    csdb::Pool::Transactions& transactions = pool.transactions();

    for (size_t idx = 0; idx < transactions.size(); ++idx) {
        {
            csdb::Pool::NewWalletInfo::AddressId addressId = {idx, csdb::Pool::NewWalletInfo::AddressType::AddressIsSource};
            addNewWalletToPool(transactions[idx].source(), addressId, *newWallets);
        }
        {
            csdb::Pool::NewWalletInfo::AddressId addressId = {idx, csdb::Pool::NewWalletInfo::AddressType::AddressIsTarget};
            addNewWalletToPool(transactions[idx].target(), addressId, *newWallets);
        }
    }

    const auto& confidants = pool.confidants();
    size_t confWalletsIndexStart = transactions.size();
    for (size_t i = 0; i < confidants.size(); ++i) {
        csdb::Pool::NewWalletInfo::AddressId addressId = {confWalletsIndexStart + i, csdb::Pool::NewWalletInfo::AddressType::AddressIsTarget};
        addNewWalletToPool(csdb::Address::from_public_key(confidants[i]), addressId, *newWallets);
    }
}

void BlockChain::close() {
    cs::Lock lock(dbLock_);
    storage_.close();
    cs::Connector::disconnect(&storage_.readBlockEvent(), this, &BlockChain::onReadFromDB);
    blockHashes_->close();
}

bool BlockChain::getTransaction(const csdb::Address& addr, const int64_t& innerId, csdb::Transaction& result) const {
    cs::Lock lock(dbLock_);
    return storage_.get_from_blockchain(addr, innerId, getLastTransaction(addr).pool_seq(), result);
}

bool BlockChain::updateContractData(const csdb::Address& abs_addr, const cs::Bytes& data) const {
    cs::Lock lock(dbLock_);
    return storage_.update_contract_data(abs_addr, data);
}

bool BlockChain::getContractData(const csdb::Address& abs_addr, cs::Bytes& data) const {
    cs::Lock lock(dbLock_);
    return storage_.get_contract_data(abs_addr, data);
}

void BlockChain::createCachesPath() {
    fs::path dbPath(cachesPath);
    boost::system::error_code code;
    const auto res = fs::is_directory(dbPath, code);

    if (!res) {
        fs::create_directory(dbPath);
    }
}

bool BlockChain::updateFromNextBlock(csdb::Pool& nextPool) {
    if (!walletsCacheUpdater_) {
        cserror() << "!walletsCacheUpdater";
        return false;
    }

    try {
        std::lock_guard lock(cacheMutex_);

        // currently block stores own round confidants, not next round:
        const auto& currentRoundConfidants = nextPool.confidants();
        walletsCacheUpdater_->loadNextBlock(nextPool, currentRoundConfidants, *this);
        walletsPools_->loadNextBlock(nextPool);
        if (!blockHashes_->onNextBlock(nextPool)) {
            cslog() << "Error writing DB structure";
        }
    }
    catch (std::exception& e) {
        cserror() << "Exc=" << e.what();
        return false;
    }
    catch (...) {
        cserror() << "Exc=...";
        return false;
    }
    return true;
}

bool BlockChain::findWalletData(const csdb::Address& address, WalletData& wallData, WalletId& id) const {
    if (address.is_wallet_id()) {
        id = address.wallet_id();
        return findWalletData(address.wallet_id(), wallData);
    }

    std::lock_guard lock(cacheMutex_);

    if (!walletIds_->normal().find(address, id)) {
        return false;
    }

    return findWalletData_Unsafe(id, wallData);
}

bool BlockChain::findWalletData(WalletId id, WalletData& wallData) const {
    std::lock_guard lock(cacheMutex_);
    return findWalletData_Unsafe(id, wallData);
}

bool BlockChain::findWalletData_Unsafe(WalletId id, WalletData& wallData) const {
    auto pubKey = getAddressByType(csdb::Address::from_wallet_id(id), AddressType::PublicKey);
    const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(pubKey.public_key());

    if (wallDataPtr) {
        wallData = *wallDataPtr;
        return true;
    }

    return false;
}

bool BlockChain::findWalletId(const WalletAddress& address, WalletId& id) const {
    if (address.is_wallet_id()) {
        id = address.wallet_id();
        return true;
    }
    else if (address.is_public_key()) {
        std::lock_guard lock(cacheMutex_);
        return walletIds_->normal().find(address, id);
    }

    cserror() << "Wrong address";
    return false;
}

bool BlockChain::getWalletId(const WalletAddress& address, WalletId& id) {
    if (address.is_wallet_id()) {
        id = address.wallet_id();
        return false;
    }
    else if (address.is_public_key()) {
        std::lock_guard lock(cacheMutex_);
        return walletIds_->normal().get(address, id);
    }

    cserror() << "Wrong address";
    return false;
}

bool BlockChain::findAddrByWalletId(const WalletId id, csdb::Address& addr) const {
    if (!walletIds_->normal().findaddr(id, addr)) {
        return false;
    }

    return true;
}

std::optional<csdb::Pool> BlockChain::recordBlock(csdb::Pool& pool, bool isTrusted) {
    const auto last_seq = getLastSeq();
    const auto pool_seq = pool.sequence();

    csdebug() << "BLOCKCHAIN> finish & store block #" << pool_seq << " to chain";

    if (last_seq + 1 != pool_seq) {
        cserror() << "BLOCKCHAIN> cannot record block #" << pool_seq << " to chain, last sequence " << last_seq;
        return std::nullopt;
    }

    pool.set_previous_hash(getLastHash());

    constexpr cs::Sequence NoSequence = std::numeric_limits<cs::Sequence>::max();
    cs::Sequence flushed_block_seq = NoSequence;

    {
        cs::Lock lock(dbLock_);

        if (deferredBlock_.is_valid()) {

            deferredBlock_.set_storage(storage_);

            if (deferredBlock_.save()) {
                flushed_block_seq = deferredBlock_.sequence();
                if (uuid_ == 0 && flushed_block_seq == 1) {
                    uuid_ = uuidFromBlock(deferredBlock_);
                    csdebug() << "Blockchain: UUID = " << uuid_;
                }
            }
            else {
                csmeta(cserror) << "Couldn't save block: " << deferredBlock_.sequence();
            }
        }
    }

    if (flushed_block_seq != NoSequence) {
        csdebug() << "---------------------------- Flush block #" << flushed_block_seq << " to disk ---------------------------";
        csdebug() << "signatures amount = " << deferredBlock_.signatures().size() << ", smartSignatures amount = " << deferredBlock_.smartSignatures().size()
                  << ", see block info above";
        csdebug() << "----------------------------------------------------------------------------------";
    }

    {
        cs::Lock lock(dbLock_);

        cs::PublicKeys lastConfidants;
        if (pool_seq > 1) {
            if (deferredBlock_.sequence() + 1 == pool_seq) {
                lastConfidants = deferredBlock_.confidants();
            }
            else {
                lastConfidants = loadBlock(pool_seq - 1).confidants();
            }
        }

        // next 2 calls order is extremely significant: finalizeBlock() may call to smarts-"enqueue"-"execute", so deferredBlock MUST BE SET properly
        deferredBlock_ = pool;
		lastSequence_ = pool.sequence();
        if (finalizeBlock(deferredBlock_, isTrusted, lastConfidants)) {
            csdebug() << "The block is correct";
        }
        else {
            csdebug() << "the signatures of the block are incorrect";
            return std::nullopt;
        }
        pool = deferredBlock_.clone();
    }
    csdetails() << "Pool #" << deferredBlock_.sequence() << ": " << cs::Utils::byteStreamToHex(deferredBlock_.to_binary().data(), deferredBlock_.to_binary().size());
    emit storeBlockEvent(pool);

    // log cached block
    csdebug() << "----------------------- Defer block #" << pool.sequence() << " until next round ----------------------";
    logBlockInfo(pool);
    csdebug() << "----------------------------------- " << pool.sequence() << " --------------------------------------";

    return std::make_optional(pool);
}

bool BlockChain::updateLastBlock(cs::RoundPackage& rPackage) {
    return updateLastBlock(rPackage, deferredBlock_);
}

bool BlockChain::updateLastBlock(cs::RoundPackage& rPackage, const csdb::Pool& poolFrom) {
    csdebug() << "BLOCKCHAIN> Starting update last block: check ...";
    //if (deferredBlock_.is_valid()) {
    //  csdebug() << "BLOCKCHAIN> Deferred block is invalid, can't update it";
    //  return false;
    //}
    if (poolFrom.is_read_only()) {
        csdebug() << "BLOCKCHAIN> Deferred block is read_only, be carefull";
        //return false;
    }

    if (poolFrom.sequence() != rPackage.poolMetaInfo().sequenceNumber) {
        csdebug() << "BLOCKCHAIN> Deferred block sequence " << poolFrom.sequence() << " doesn't equal to that in the roundPackage " << rPackage.poolMetaInfo().sequenceNumber << ", can't update it";
        return false;
    }
    if (poolFrom.signatures().size() >= rPackage.poolSignatures().size()) {
        csdebug() << "BLOCKCHAIN> Deferred block has more or the same amount Signatures, than received roundPackage, can't update it";
        return true;
    }
    if (poolFrom.previous_hash() != rPackage.poolMetaInfo().previousHash) {
        csdebug() << "BLOCKCHAIN> Deferred block PREVIOUS HASH doesn't equal to that in the roundPackage, can't update it";
        return false;
    }
    csdebug() << "BLOCKCHAIN> Ok";

    csdb::Pool tmpPool;
    tmpPool.set_sequence(poolFrom.sequence());
    tmpPool.set_previous_hash(poolFrom.previous_hash());
    tmpPool.add_real_trusted(cs::Utils::maskToBits(rPackage.poolMetaInfo().realTrustedMask));
    csdebug() << "BLOCKCHAIN> new mask set to deferred block: " << cs::TrustedMask::toString(rPackage.poolMetaInfo().realTrustedMask);
    tmpPool.add_number_trusted(static_cast<uint8_t>(rPackage.poolMetaInfo().realTrustedMask.size()));
    tmpPool.setRoundCost(poolFrom.roundCost());
    tmpPool.set_confidants(poolFrom.confidants());
    for (auto& it : poolFrom.transactions()) {
        tmpPool.add_transaction(it);
    }
    tmpPool.add_user_field(0, rPackage.poolMetaInfo().timestamp);
    for (auto& it : poolFrom.smartSignatures()) {
        tmpPool.add_smart_signature(it);
    }
    csdb::Pool::NewWallets* newWallets = tmpPool.newWallets();
    const csdb::Pool::NewWallets& defWallets = poolFrom.newWallets();
    if (!newWallets) {
        cserror() << "newPool is read-only";
        return false;
    }

    for (auto it : defWallets) {
        newWallets->push_back(it);
    }

    if (rPackage.poolMetaInfo().sequenceNumber > 1) {
        tmpPool.add_number_confirmations(poolFrom.numberConfirmations());
        tmpPool.add_confirmation_mask(poolFrom.roundConfirmationMask());
        tmpPool.add_round_confirmations(poolFrom.roundConfirmations());
    }

    return deferredBlockExchange(rPackage, tmpPool);
}

bool BlockChain::deferredBlockExchange(cs::RoundPackage& rPackage, const csdb::Pool& newPool) {
    deferredBlock_ = csdb::Pool{};
    deferredBlock_ = newPool;
    auto tmp = rPackage.poolSignatures();
    deferredBlock_.set_signatures(tmp);
    deferredBlock_.compose();
    Hash tempHash;
    auto hash = deferredBlock_.hash().to_binary();
    std::copy(hash.cbegin(), hash.cend(), tempHash.data());
    if (NodeUtils::checkGroupSignature(deferredBlock_.confidants(), rPackage.poolMetaInfo().realTrustedMask, rPackage.poolSignatures(), tempHash)) {
        csmeta(csdebug) << "The number of signatures is sufficient and all of them are OK!";

    }
    else {
        cswarning() << "Some of Pool Signatures aren't valid. The pool will not be written to DB. It will be automatically written, when we get proper data";
        return false;
    }
    return true;
}

bool BlockChain::storeBlock(csdb::Pool& pool, bool bySync) {
    csdebug() << csfunc() << ":";

    const auto lastSequence = getLastSeq();
    const auto poolSequence = pool.sequence();

    if (poolSequence <= lastSequence) {
        // ignore
        csdebug() << "BLOCKCHAIN> ignore oudated block #" << poolSequence << ", last written #" << lastSequence;
        // it is not error, so caller code nothing to do with it
        return true;
    }

    if ((pool.numberConfirmations() == 0 || pool.roundConfirmations().size() == 0) && pool.sequence() > 1) {
        return false;
    }

    if (poolSequence == lastSequence) {
        std::lock_guard lock(dbLock_);

        if (!deferredBlock_.signatures().empty()) {
            // ignore
            csdebug() << "BLOCKCHAIN> ignore oudated block #" << poolSequence << ", last written #" << lastSequence;
            // it is not error, so caller code nothing to do with it
            return true;
        }
        else {
            csdebug() << "BLOCKCHAIN> we have to rewrite #" << poolSequence;
            // removeLastBlock();
        }
    }

    if (poolSequence == lastSequence + 1) {
        if (pool.previous_hash() != getLastHash()) {
            csdebug() << "BLOCKCHAIN> new pool\'s prev. hash does not equal to current last hash, remove own last block and cancel store operation";
            if (getLastHash().is_empty()) {
                cserror() << "BLOCKCHAIN> own last hash is empty";
            }
            if (pool.previous_hash().is_empty()) {
                cserror() << "BLOCKCHAIN> new pool\'s prev. hash is empty";
            }
            removeLastBlock();
            return false;
        }

        setTransactionsFees(pool);

        // update wallet ids
        if (bySync) {
            // ready-to-record block does not require anything
            csdebug() << "BLOCKCHAIN> store block #" << poolSequence << " to chain, update wallets ids";
            updateWalletIds(pool, *walletsCacheUpdater_);
        }
        else {
            csdebug() << "BLOCKCHAIN> store block #" << poolSequence << " add new wallets to pool";
            addNewWalletsToPool(pool);
        }

        // write immediately
        if (recordBlock(pool, false).has_value()) {
            csdebug() << "BLOCKCHAIN> block #" << poolSequence << " has recorded to chain successfully";
            // unable to call because stack overflow in case of huge written blocks amount possible:
            // testCachedBlocks();
            return true;
        }

        csdebug() << "BLOCKCHAIN> failed to store block #" << poolSequence << " to chain";
        removeLastBlock();
        return false;
    }

    if (cachedBlocks_.count(poolSequence) > 0) {
        csdebug() << "BLOCKCHAIN> ignore duplicated block #" << poolSequence << " in cache";
        // it is not error, so caller code nothing to do with it
        return true;
    }
    // cache block for future recording
    cachedBlocks_.emplace(poolSequence, BlockMeta{pool, bySync});
    csdebug() << "BLOCKCHAIN> cache block #" << poolSequence << " signed by " << pool.signatures().size()
        << " nodes for future (" << cachedBlocks_.size() << " total)";
    cachedBlockEvent(poolSequence);
    // cache always successful
    return true;
}

void BlockChain::testCachedBlocks() {
    csdebug() << "BLOCKCHAIN> test cached blocks";
    if (cachedBlocks_.empty()) {
        csdebug() << "BLOCKCHAIN> no cached blocks";
        return;
    }

    auto lastSeq = getLastSeq() + 1;
    // clear unnecessary sequence
    if (cachedBlocks_.cbegin()->first < lastSeq) {
        auto it = cachedBlocks_.lower_bound(lastSeq);
        if (it != cachedBlocks_.begin()) {
            csdebug() << "BLOCKCHAIN> Remove outdated blocks up to #" << (*it).first << " from cache";
            cachedBlocks_.erase(cachedBlocks_.begin(), it);
        }
    }

    while (!cachedBlocks_.empty()) {
        auto firstBlockInCache = cachedBlocks_.begin();

        if ((*firstBlockInCache).first == lastSeq) {
            csdebug() << "BLOCKCHAIN> Retrieve required block #" << lastSeq << " from cache";
            // retrieve and use block if it is exactly what we need:

            const bool ok = storeBlock((*firstBlockInCache).second.pool, (*firstBlockInCache).second.by_sync);
            cachedBlocks_.erase(firstBlockInCache);
            if (!ok) {
                cserror() << "BLOCKCHAIN> Failed to record cached block to chain, drop it & wait to request again";
                break;
            }
            ++lastSeq;
        }
        else {
            // stop processing, we have not got required block in cache yet
            csdebug() << "BLOCKCHAIN> Stop store block from cache. Next blocks in cache #" << (*firstBlockInCache).first;
            break;
        }
    }
}

const cs::ReadBlockSignal& BlockChain::readBlockEvent() const {
    return storage_.readBlockEvent();
}

std::size_t BlockChain::getCachedBlocksSize() const {
    return cachedBlocks_.size();
}

std::vector<BlockChain::SequenceInterval> BlockChain::getRequiredBlocks() const {
	cs::Sequence seq = getLastSeq();
    const auto firstSequence = seq + 1;
    const auto currentRoundNumber = cs::Conveyer::instance().currentRoundNumber();

    if (firstSequence >= currentRoundNumber) {
        return std::vector<SequenceInterval>();
    }

    const auto roundNumber = currentRoundNumber > 0 ? std::max(firstSequence, currentRoundNumber - 1) : 0;

    // return at least [next, 0] or [next, currentRoundNumber]:
    std::vector<SequenceInterval> vec{std::make_pair(firstSequence, roundNumber)};

    // always point to last interval
    auto firstUpper = cachedBlocks_.upper_bound(firstSequence);

    if (firstUpper != cachedBlocks_.end()) {
        auto sequence = firstUpper->first;
        vec[0].second = sequence - 1;

        while ((++firstUpper) != cachedBlocks_.end()) {
            ++sequence;
            if (firstUpper->first != sequence) {
                vec.emplace_back(std::make_pair(sequence, firstUpper->first - 1));
                sequence = firstUpper->first;
            }
        }
    }

    // add last interval [final + 1, end]
    if (!cachedBlocks_.empty()) {
        const auto lastCahedBlock = cachedBlocks_.crbegin()->first;
        if (roundNumber > lastCahedBlock) {
            vec.emplace_back(std::make_pair(lastCahedBlock, roundNumber));
        }
    }

    return vec;
}

void BlockChain::setTransactionsFees(TransactionsPacket& packet) {
    fee::setCountedFees(packet.transactions());
}

void BlockChain::setTransactionsFees(csdb::Pool& pool) {
    fee::setCountedFees(pool.transactions());
}

void BlockChain::setTransactionsFees(std::vector<csdb::Transaction>& transactions) {
    fee::setCountedFees(transactions);
}

void BlockChain::setTransactionsFees(std::vector<csdb::Transaction>& transactions, const cs::Bytes&) {
    fee::setCountedFees(transactions);
}

const csdb::Address& BlockChain::getGenesisAddress() const {
    return genesisAddress_;
}

csdb::Address BlockChain::getAddressByType(const csdb::Address& addr, AddressType type) const {
    csdb::Address addr_res{};
    switch (type) {
        case AddressType::PublicKey:
            if (addr.is_public_key() || !findAddrByWalletId(addr.wallet_id(), addr_res)) {
                addr_res = addr;
            }

            break;
        case AddressType::Id:
            uint32_t _id;
            if (findWalletId(addr, _id)) {
                addr_res = csdb::Address::from_wallet_id(_id);
            }

            break;
    }
    return addr_res;
}

bool BlockChain::isEqual(const csdb::Address& laddr, const csdb::Address& raddr) const {
    if (getAddressByType(laddr, AddressType::PublicKey) == getAddressByType(raddr, AddressType::PublicKey)) {
        return true;
    }

    return false;
}

uint32_t BlockChain::getTransactionsCount(const csdb::Address& addr) {
    std::lock_guard lock(cacheMutex_);

    auto pubKey = getAddressByType(addr, AddressType::PublicKey);
    const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(pubKey.public_key());

    if (!wallDataPtr) {
        return 0;
    }

    return static_cast<uint32_t>(wallDataPtr->transNum_);
}

//uint64_t BlockChain::initUuid() const {
//    // protects from subsequent calls
//    if (uuid_ != 0) {
//        return uuid_;
//    }
//    // lookup in hashes
//    if (!blockHashes_->empty()) {
//        const auto& hashes = blockHashes_->getHashes();
//        if (hashes.size() > 1) {
//            const auto tmp = uuidFromHash(hashes[1]);
//            if (tmp != 0) {
//                return tmp;
//            }
//        }
//    }
//    // lookup in chain
//    return uuidFromBlock(loadBlock(1));
//}

csdb::TransactionID BlockChain::getLastTransaction(const csdb::Address& addr) const {
    std::lock_guard lock(cacheMutex_);

    auto pubKey = getAddressByType(addr, AddressType::PublicKey);
    const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(pubKey.public_key());

    if (!wallDataPtr) {
        return csdb::TransactionID();
    }

    return wallDataPtr->lastTransaction_;
}

cs::Sequence BlockChain::getPreviousPoolSeq(const csdb::Address& addr, cs::Sequence ps) const {
    std::lock_guard lock(dbLock_);
    return storage_.get_previous_transaction_block(getAddressByType(addr, AddressType::PublicKey), ps);
}

std::pair<cs::Sequence, uint32_t> BlockChain::getLastNonEmptyBlock() {
    std::lock_guard lock(dbLock_);
    return std::make_pair(lastNonEmptyBlock_.poolSeq, lastNonEmptyBlock_.transCount);
}

std::pair<cs::Sequence, uint32_t> BlockChain::getPreviousNonEmptyBlock(cs::Sequence seq) {
    std::lock_guard lock(dbLock_);
    const auto it = previousNonEmpty_.find(seq);

    if (it != previousNonEmpty_.end()) {
        return std::make_pair(it->second.poolSeq, it->second.transCount);
    }

    return std::pair<cs::Sequence, uint32_t>(cs::kWrongSequence, 0);
}

cs::Sequence BlockChain::getLastSeq() const{
	return lastSequence_;
}
