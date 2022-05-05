#include <base58.h>
#include <csdb/currency.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <limits>

#ifdef DBSQL
#include <dbsql/roundinfo.hpp>
#endif
#include <csnode/blockchain.hpp>
#include <csnode/blockhashes.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>
#include <csnode/fee.hpp>
#include <csnode/nodeutils.hpp>
#include <csnode/node.hpp>
#include <csnode/transactionsindex.hpp>
#include <csnode/transactionsiterator.hpp>
#include <csnode/configholder.hpp>
#include <solver/smartcontracts.hpp>

#include <boost/filesystem.hpp>


using namespace cs;
namespace fs = boost::filesystem;

namespace {
const char* cachesPath = "./caches";
const char* kLogPrefix = "BLOCKCHAIN: ";
} // namespace

BlockChain::BlockChain(csdb::Address genesisAddress, csdb::Address startAddress, bool recreateIndex)
: good_(false)
, dbLock_()
, genesisAddress_(genesisAddress)
, startAddress_(startAddress)
, walletIds_(new WalletsIds)
, walletsCacheStorage_(new WalletsCache(*walletIds_))
, cacheMutex_() {
    createCachesPath();
    walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();
    blockHashes_ = std::make_unique<cs::BlockHashes>(cachesPath);
    cachedBlocks_ = std::make_unique<cs::PoolCache>(cachesPath);
    trxIndex_ = std::make_unique<cs::TransactionsIndex>(*this, cachesPath, recreateIndex);
}

BlockChain::~BlockChain() {}

void BlockChain::subscribeToSignals() {
    // the order of two following calls matters
    cs::Connector::connect(&storage_.readBlockEvent(), trxIndex_.get(), &TransactionsIndex::onReadFromDb);
    cs::Connector::connect(&storage_.readBlockEvent(), this, &BlockChain::onReadFromDB);
    cs::Connector::connect(&storage_.readingStartedEvent(), trxIndex_.get(), &TransactionsIndex::onStartReadFromDb);
    cs::Connector::connect(&storage_.readingStartedEvent(), this, &BlockChain::onStartReadFromDB);

    cs::Connector::connect(&storage_.readingStoppedEvent(), trxIndex_.get(), &TransactionsIndex::onDbReadFinished);
}

bool BlockChain::bindSerializationManToCaches(
    cs::CachesSerializationManager* serializationManPtr,
    std::set<cs::PublicKey>& initialConfidants
) {
    if (!serializationManPtr) {
        cserror() << "NO SERIALIZATION MANAGER PROVIDED!";
        return false;
    }

    serializationManPtr_ = serializationManPtr;

    serializationManPtr_->bind(*this, initialConfidants);
    serializationManPtr_->bind(*walletsCacheStorage_);
    serializationManPtr_->bind(*walletIds_);

    return true;
}

bool BlockChain::tryQuickStart(
    cs::CachesSerializationManager* serializationManPtr,
    std::set<cs::PublicKey>& initialConfidants
) {
    cslog() << "Try QUICK START...";
    if (!bindSerializationManToCaches(serializationManPtr, initialConfidants)) {
        return false;
    }

    std::set<cs::PublicKey> reserveConf;
    for (auto it : initialConfidants) {
        reserveConf.insert(it);
    }
    bool ok = serializationManPtr_->load();

    if (ok) {
        cslog() << "Caches for QUICK START loaded successfully!";
    } else {
        cswarning() << "Could not load caches for QUICK START, continue with slow start :(";
        for (auto it : reserveConf) {
            initialConfidants.insert(it);
        }
    }

    return ok;
}

bool BlockChain::init(
    const std::string& path,
    cs::CachesSerializationManager* serializationManPtr,
    std::set<cs::PublicKey>& initialConfidants,
    cs::Sequence newBlockchainTop
  ) {
    cs::Connector::connect(&this->removeBlockEvent, trxIndex_.get(), &TransactionsIndex::onRemoveBlock);

    lastSequence_ = 0;
    bool successfulQuickStart = false;

    if (newBlockchainTop == cs::kWrongSequence) {
        if (trxIndex_->recreate()) {
            cslog() << "Cannot use QUICK START, trxIndex has to be recreated";
            bindSerializationManToCaches(serializationManPtr, initialConfidants);
        }
        else {
            successfulQuickStart = tryQuickStart(serializationManPtr, initialConfidants);
        }
    }

    cs::Sequence firstBlockToReadInDatabase = 0;
    if (successfulQuickStart) {
        if (lastSequence_ != 0) {
            firstBlockToReadInDatabase = lastSequence_ + 1;
        }

        csinfo() << "QUICK START! lastSequence_   is " << lastSequence_.load();
        csinfo() << "QUICK START! first block to read in database is " << firstBlockToReadInDatabase;
    }
    else {
        cslog() << "SLOW START...";
    }

    cslog() << kLogPrefix << "Trying to open DB...";

    size_t totalLoaded = 0;
    bool checkTrxIndexRecreate = true;

    csdb::Storage::OpenCallback progress = [&](const csdb::Storage::OpenProgress& progress) {
        if (checkTrxIndexRecreate) {
          checkTrxIndexRecreate = false;
          if (trxIndex_->recreate() && successfulQuickStart) {
              cslog() << "Blockchain: TrxIndex must be recreated, cancel QUICK START... Restart NODE, please";
              trxIndex_->invalidate();
              return true;
          }
        }

        totalLoaded = progress.poolsProcessed;
        if (totalLoaded % 1000 == 0) {
            std::cout << '\r' << WithDelimiters(progress.poolsProcessed) << std::flush;
        }
        return false;
    };

    if (!storage_.open(path, progress, newBlockchainTop, firstBlockToReadInDatabase)) {
        cserror() << kLogPrefix << "Couldn't open database at " << path;
        return false;
    }

    if (newBlockchainTop != cs::kWrongSequence) {
        return true;
    }

    cslog() << "\rDB is opened, loaded " << WithDelimiters(totalLoaded) << " blocks";

    if (storage_.last_hash().is_empty()) {
        csdebug() << "Last hash is empty...";
        if (storage_.size()) {
            cserror() << "failed!!! Delete the Database!!! It will be restored from nothing...";
            return false;
        }
        if (successfulQuickStart) {
            serializationManPtr_->clear();
        }
        writeGenesisBlock();
    }
    else {
        if (!postInitFromDB(successfulQuickStart)) {
            return false;
        }
    }

    good_ = true;
    blocksToBeRemoved_ = totalLoaded - 1; // any amount to remave after start
    return true;
}

bool BlockChain::isGood() const {
    return good_;
}

uint64_t BlockChain::uuid() const {
    return uuid_;
}

void BlockChain::onStartReadFromDB(cs::Sequence lastWrittenPoolSeq) {
    if (lastWrittenPoolSeq > 0) {
        cslog() << kLogPrefix << "start reading blocks from DB, " 
                << "last is " << WithDelimiters(lastWrittenPoolSeq);
    }
}

void BlockChain::onReadFromDB(csdb::Pool block, bool* shouldStop) {
    auto blockSeq = block.sequence();
    lastSequence_ = blockSeq;
    if (blockSeq == 1) {
        cs::Lock lock(dbLock_);
        uuid_ = uuidFromBlock(block);
        csdebug() << kLogPrefix << "UUID = " << uuid_;
    }

    if (!updateWalletIds(block, *walletsCacheUpdater_.get())) {
        cserror() << kLogPrefix << "updateWalletIds() failed on block #" << block.sequence();
        *shouldStop = true;
    }
    else {
        if (!blockHashes_->onNextBlock(block)) {
            cserror() << kLogPrefix << "blockHashes_->onReadBlock(block) failed on block #" << block.sequence();
            *shouldStop = true;
        }
        else {
            if (block.transactions_count() > 0) {
                const auto block_time = BlockChain::getBlockTime(block);
                for (auto& t : block.transactions()) {
                    t.set_time(block_time);
                }
            }
            updateNonEmptyBlocks(block);
            walletsCacheUpdater_->loadNextBlock(block, block.confidants(), *this);
        }
    }
}

inline void BlockChain::updateNonEmptyBlocks(const csdb::Pool& pool) {
    const auto transactionsCount = pool.transactions_count();

    if (transactionsCount > 0) {
        totalTransactionsCount_ += transactionsCount;

        if (lastNonEmptyBlock_.transCount && pool.sequence() != lastNonEmptyBlock_.poolSeq) {
            previousNonEmpty_[pool.sequence()] = lastNonEmptyBlock_;
        }

        lastNonEmptyBlock_.poolSeq = pool.sequence();
        lastNonEmptyBlock_.transCount = static_cast<uint32_t>(transactionsCount);
    }
}

bool BlockChain::postInitFromDB(bool successfulQuickStart) {
    auto func = [](const cs::PublicKey& key, const WalletData& wallet) {
        double bal = wallet.balance_.to_double();
        if (bal < -std::numeric_limits<double>::min()) {
            csdebug() << kLogPrefix << "Wallet with negative balance (" << bal << ") detected: "
                      << cs::Utils::byteStreamToHex(key.data(), key.size()) << " ("
                      << EncodeBase58(key.data(), key.data() + key.size()) << ")";
        }
        return true;
    };
    walletsCacheStorage_->iterateOverWallets(func);
    if (successfulQuickStart) {
        emit stopReadingBlocksEvent(totalTransactionsCount_);
    }
    return true;
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
        if (deferredBlock_.user_field_ids().count(kFieldTimestamp) > 0) {
            return deferredBlock_.user_field(kFieldTimestamp).value<std::string>();
        }
        else {
            return std::string("0");
        }
    }
    else {
        return getLastBlock().user_field(kFieldTimestamp).value<std::string>();
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
    cswarning() << kLogPrefix << "Adding the genesis block";

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

    csdebug() << kLogPrefix << "Genesis block completed ... trying to save";

    /*ignored =*/ finalizeBlock(genesis, true, cs::PublicKeys{});
    /*ignored =*/ applyBlockToCaches(genesis);
    deferredBlock_ = genesis;
    emit storeBlockEvent(deferredBlock_);

    csdebug() << genesis.hash().to_string();

    //uint32_t bSize;
    //auto p = genesis.to_byte_stream(bSize);
    //csdebug() << "Genesis" << cs::Utils::byteStreamToHex(p, bSize);
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

csdb::Pool BlockChain::loadBlockForSync(const cs::Sequence sequence) const {
    if (uncertainLastBlockFlag_ && uncertainSequence_ == sequence) {
        return csdb::Pool{};
    }
    else {
        return loadBlock(sequence);
    }
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

    if (deferredBlock_.is_valid() && deferredBlock_.sequence() == transId.pool_seq()) {
        transaction = deferredBlock_.transaction(transId).clone();
        transaction.set_time(BlockChain::getBlockTime(deferredBlock_));
    }
    else {
        transaction = storage_.transaction(transId).clone();
        auto tmp = storage_.pool_load(transId.pool_seq());
        transaction.set_time(BlockChain::getBlockTime(tmp));
    }

    return transaction;
}

// - remove the last block from the top of blockchain
// - remove pair (hash, sequence) from cache (blockHashes_)
// - decrement the last sequence by 1
// - undo all transactions / new wallets
void BlockChain::removeLastBlock() {
    if (blocksToBeRemoved_ == 0) {
        csmeta(csdebug) << kLogPrefix << "There are no blocks, allowed to be removed";
        return;
    }
    //--blocksToBeRemoved_;
	cs::Sequence remove_seq = lastSequence_;
	csdb::PoolHash remove_hash = blockHashes_->find(remove_seq);
    csmeta(csdebug) << remove_seq;
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
        csmeta(cserror) << kLogPrefix << "Error! Removed pool is not valid";

		if (remove_hash.is_empty()) {
			cserror() << kLogPrefix << "storage is corrupted, storage rescan is required, removed hash is empty";
			return;
		}
        csinfo() << kLogPrefix << "pool to be deleted is not valid, trying to use repair_remove";
		{
			std::lock_guard lock(dbLock_);
			if (!storage_.pool_remove_last_repair(remove_seq, remove_hash)) {
				cserror() << kLogPrefix << "storage is corrupted, storage rescan is required";
				return;
			}
		}

		cswarning() << kLogPrefix << "Wallets balances maybe invalidated, storage rescan required";
    }
    else {
        // just removed pool is valid

        if (!(remove_hash == pool.hash())) {
            cswarning() << kLogPrefix << "Hashes cache is corrupted, storage rescan is required: last: " 
                << remove_hash.to_string() << ", pool: " << pool.hash().to_string() 
                << ", total hashes: " << blockHashes_->size() 
                << ", total pools: " << getLastSeq() + 1ULL;
            remove_hash = pool.hash();
        }

        if (pool.sequence() == 0) {
            csmeta(cswarning) << kLogPrefix << "Attempt to remove Genesis block !!!!!";
            return;
        }

		// such operations are only possible on valid pool:
        totalTransactionsCount_ -= pool.transactions().size();
		walletsCacheUpdater_->loadNextBlock(pool, pool.confidants(), *this, true);
		// remove wallets exposed by the block
		removeWalletsInPoolFromCache(pool);
		// signal all subscribers, transaction index is still consistent up to removed block!
		emit removeBlockEvent(pool);

        if (lastNonEmptyBlock_.poolSeq == pool.sequence()) {
            lastNonEmptyBlock_ = previousNonEmpty_[lastNonEmptyBlock_.poolSeq];
            previousNonEmpty_.erase(pool.sequence());
        }
    }

    // to be sure, try to remove both sequence and hash
    if (!blockHashes_->remove(remove_seq)) {
        blockHashes_->remove(remove_hash);
    }
    --lastSequence_;

    csmeta(csdebug) << kLogPrefix << "done";
}

bool BlockChain::compromiseLastBlock(const csdb::PoolHash& desired_hash) {
    csdb::Pool last_block = csdb::Pool{};
    {
        cs::Lock lock(dbLock_);

        if (deferredBlock_.is_valid()) {
            last_block = deferredBlock_.clone();
        }
    }
    if (!last_block.is_valid()) {
        cserror() << kLogPrefix << "can only compromise the deferred block, not flushed yet";
        uncertainLastBlockFlag_ = false;
        return false;
    }

    const auto seq = last_block.sequence();
    const auto current = last_block.hash();
    const auto desired = desired_hash;

    if (uncertainLastBlockFlag_ && seq != uncertainSequence_) {
        cswarning() << kLogPrefix << "change uncertain sequence from " << uncertainSequence_ << " to " << seq;
    }
    uncertainSequence_ = seq;
    if (uncertainLastBlockFlag_ && current != uncertainHash_) {
        cswarning() << kLogPrefix << "change uncertain hash from " << uncertainHash_.to_string() << " to " << current.to_string();
    }
    uncertainHash_ = current;
    if (uncertainLastBlockFlag_ && desired != desiredHash_) {
        cswarning() << kLogPrefix << "change desired hash from " << desiredHash_.to_string() << " to " << desired.to_string();
    }
    desiredHash_ = desired;

    uncertainLastBlockFlag_ = true;
    cslog() << kLogPrefix << "block " << WithDelimiters(uncertainSequence_) << ", hash " << uncertainHash_.to_string()
        << " is uncertain, desired hash " << desiredHash_.to_string();

    /*signal*/ uncertainBlock(uncertainSequence_);
    return true;
}

void BlockChain::updateLastTransactions(const std::vector<std::pair<cs::PublicKey, csdb::TransactionID>>& updates) {
    std::lock_guard l(cacheMutex_);
    walletsCacheUpdater_->updateLastTransactions(updates);
}

/*static*/
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

/*static*/
uint64_t BlockChain::getBlockTime(const csdb::Pool& block) noexcept {
    if (block.is_valid()) {
        if (block.user_field_ids().count(kFieldTimestamp) > 0) {
            std::string tmp = block.user_field(kFieldTimestamp).value<std::string>();
            try {
                return std::stoull(tmp);
            }
            catch (...) {
                csdebug() << kLogPrefix << "block " << WithDelimiters(block.sequence()) << " contains incorrect timestamp value " << tmp;
            }
        }
    }
    return 0;
}

void BlockChain::removeWalletsInPoolFromCache(const csdb::Pool& pool) {
    try {
        std::lock_guard lock(cacheMutex_);
        const csdb::Pool::NewWallets& newWallets = pool.newWallets();

        for (const auto& newWall : newWallets) {
            csdb::Address newWallAddress;
            if (!pool.getWalletAddress(newWall, newWallAddress)) {
                cserror() << kLogPrefix << "Wrong new wallet data";
                return;
            }
            if (!walletIds_->normal().remove(newWallAddress)) {
                cswarning() << kLogPrefix << "Wallet was not removed " << newWallAddress.to_string();
            }
        }

        for (const auto& it : pool.transactions()) {
            if (cs::SmartContracts::is_deploy(it)) {
                if (!walletIds_->normal().remove(it.target())) {
                    cswarning() << kLogPrefix << "Contract address was not removed: " << it.target().to_string();
                }
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
    if (pool.user_field_ids().count(kFieldTimestamp) > 0) {
        csdebug() << " time: " << pool.user_field(kFieldTimestamp).value<std::string>().c_str();
    }
    csdebug() << " previous hash: " << pool.previous_hash().to_string();
    csdebug() << " hash(" << pool.sequence() << "): " << pool.hash().to_string();
    csdebug() << " last storage size: " << getSize();
}

bool BlockChain::finalizeBlock(csdb::Pool& pool, bool isTrusted, cs::PublicKeys lastConfidants) {
    if (!pool.compose()) {
        csmeta(cserror) << kLogPrefix << "Couldn't compose block: " << pool.sequence();
        return false;
    }

    cs::Sequence currentSequence = pool.sequence();
    const auto& confidants = pool.confidants();
    const auto& signatures = pool.signatures();
    const auto& realTrusted = pool.realTrusted();
    if (currentSequence > 1) {
        csdebug() << kLogPrefix << "Finalize: starting confidants validation procedure (#" << currentSequence << "):";

        cs::Bytes trustedToHash;
        cs::ODataStream tth(trustedToHash);
        tth << currentSequence;
        tth << confidants;

        cs::Hash trustedHash = cscrypto::calculateHash(trustedToHash.data(), trustedToHash.size());

        const cs::Signatures& sigs = pool.roundConfirmations();
        const auto& confMask = cs::Utils::bitsToMask(pool.numberConfirmations(), pool.roundConfirmationMask());
        // for debugging only delete->
        csdebug() << kLogPrefix << "Mask size = " << confMask.size() << " for next confidants:";
        for (auto& it : lastConfidants) {
            csdebug() << cs::Utils::byteStreamToHex(it.data(), it.size());
        }
        // <-delete
        if (!BlockChain::isBootstrap(pool) && confMask.size() > 1) {
            if (!NodeUtils::checkGroupSignature(lastConfidants, confMask, sigs, trustedHash)) {
                csdebug() << kLogPrefix << "           The Confidants confirmations are not OK";
                return false;
            }
            else {
                csdebug() << kLogPrefix << "           The Confidants confirmations are OK";
            }
        }
        else {
            // TODO: add SS or bootstrap confidants PKey to the prevConfidants
        }
    }

    if (signatures.empty() && (!isTrusted || pool.sequence() != 0)) {
        csmeta(csdebug) << kLogPrefix << "The pool #" << pool.sequence() << " doesn't contain signatures";
        return false;
    }

    if (signatures.size() < static_cast<size_t>(cs::Utils::maskValue(realTrusted)) && !isTrusted && pool.sequence() != 0) {
        csmeta(csdebug) << kLogPrefix << "The number of signatures is insufficient";
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
            csmeta(csdebug) << kLogPrefix << "The number of signatures is sufficient and all of them are OK";
        }
        else {
            cswarning() << kLogPrefix << "Some of Pool Signatures aren't valid. The pool will not be written to DB. It will be automatically written, when we get proper data";
            return false;
        }
    }
    else {
        csmeta(csdebug) << kLogPrefix << "Genesis block will be written without signatures verification";
    }
    // pool signatures check: end

    if (pool.transactions_count() > 0) {
        const auto block_time = BlockChain::getBlockTime(pool);
        for (auto& t : pool.transactions()) {
            t.set_time(block_time);
        }
    }

    csmeta(csdetails) << kLogPrefix << "last hash: " << pool.hash().to_string();
    return true;
}

bool BlockChain::applyBlockToCaches(const csdb::Pool& pool) {
    if (!walletsCacheUpdater_) {
        cserror() << "apply block to caches: wallets cache updater unitialized";
        return false;
    }

    csdebug() << kLogPrefix << "store block #" << pool.sequence() << " to chain, update wallets ids";
    updateWalletIds(pool, *walletsCacheUpdater_);

    // ATTENTION! Due to undesired side effect trxIndex_ must be updated prior to wallets caches
    // update transactions index
    trxIndex_->update(pool);

    // update wallet caches

    // former updateFromNextBlock(pool) method:
    try {
        std::lock_guard lock(cacheMutex_);
        // currently block stores own round confidants, not next round:
        const auto& currentRoundConfidants = pool.confidants();
        walletsCacheUpdater_->loadNextBlock(pool, currentRoundConfidants, *this);

        if (!blockHashes_->onNextBlock(pool)) {
            cslog() << kLogPrefix << "Error updating block hashes storage";
        }

        // update non-empty block storage
        updateNonEmptyBlocks(pool);
    }
    catch (std::exception & e) {
        cserror() << "apply block to caches, exception: " << e.what();
        return false;
    }
    catch (...) {
        cserror() << "apply block to caches, unexpected exception";
        return false;
    }

    if (serializationManPtr_
        && pool.sequence()
        && pool.sequence() % kQuickStartSaveCachesInterval == 0
        && !serializationManPtr_->save(pool.sequence())) {
        cserror() << "Cannot save caches with version " << pool.sequence();
    }

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

uint64_t BlockChain::getWalletsCount() const {
    std::lock_guard lock(cacheMutex_);
    return walletsCacheStorage_->getCount();
}

void BlockChain::getTransactions(Transactions& transactions, csdb::Address address, uint64_t offset, uint64_t limit) {
    for (auto trIt = cs::TransactionsIterator(*this, address); trIt.isValid(); trIt.next()) {
        if (offset > 0) {
            --offset;
            continue;
        }

        transactions.push_back(*trIt);
        transactions.back().set_time(BlockChain::getBlockTime(trIt.getPool()));

        if (--limit == 0)
            break;
    }
}

void BlockChain::getAccountRegTime(uint64_t& aTime, csdb::Address address) {
    for (auto trIt = cs::TransactionsIterator(*this, address); trIt.isValid(); trIt.next()) {
        aTime=BlockChain::getBlockTime(trIt.getPool());
    }
}

void BlockChain::getTransactionsUntill(Transactions& transactions, csdb::Address address, csdb::TransactionID id, uint16_t flagg) {
    for (auto trIt = cs::TransactionsIterator(*this, address); trIt.isValid(); trIt.next()) {
        if (id.pool_seq() + 1 > cs::Conveyer::instance().currentRoundNumber() 
            && id.pool_seq() > trIt->id().pool_seq()) {
            break;
        }
        if (id.pool_seq() < trIt->id().pool_seq() || (id.pool_seq() == trIt->id().pool_seq() && id.index() < trIt->id().index())) {
            if (flagg == 1) {
                bool match = false;
                if (trIt->target().is_public_key())
                {
                    if (trIt->target().public_key() == address.public_key()) {
                        match = true;
                    }
                }
                if (trIt->target().is_wallet_id())
                {
                    csdb::Address pKey;
                    findAddrByWalletId(trIt->target().wallet_id(), pKey);
                    if (pKey.public_key() == address.public_key()) {
                        match = true;
                    }
                }
                if(match)
                {
                    transactions.push_back(*trIt);
                    transactions.back().set_time(getBlockTime(trIt.getPool()));
                }
            }
            if (flagg ==2){
                bool match = false;
                if (trIt->source().is_public_key())
                {
                    if (trIt->source().public_key() == address.public_key()) {
                        match = true;
                    }
                }
                if (trIt->source().is_wallet_id())
                {
                    csdb::Address pKey;
                    findAddrByWalletId(trIt->source().wallet_id(), pKey);
                    if (pKey.public_key() == address.public_key()) {
                        match = true;
                    }
                }
                if (match)
                {
                    transactions.push_back(*trIt);
                    transactions.back().set_time(getBlockTime(trIt.getPool()));
                }
            }

            if (flagg == 3) {
                transactions.push_back(*trIt);
                transactions.back().set_time(getBlockTime(trIt.getPool()));
            }
        }
        //if (--limit == 0)
        //    break;
    }
}

bool BlockChain::updateWalletIds(const csdb::Pool& pool, WalletsCache::Updater& proc) {
    try {
        std::lock_guard lock(cacheMutex_);

        const csdb::Pool::NewWallets& newWallets = pool.newWallets();
        for (const auto& newWall : newWallets) {
            csdb::Address newWallAddress;
            if (!pool.getWalletAddress(newWall, newWallAddress)) {
                cserror() << kLogPrefix << "Wrong new wallet data";
                return false;
            }

            if (!insertNewWalletId(newWallAddress, newWall.walletId_, proc)) {
                cserror() << kLogPrefix << "Wallet was already added as new";
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
        cserror() << kLogPrefix << "Cannot add new wallet";
        return false;
    }

    return true;
}

bool BlockChain::addNewWalletsToPool(csdb::Pool& pool) {
    csdebug() << kLogPrefix << "store block #" << pool.sequence() << " add new wallets to pool";

    csdb::Pool::NewWallets* newWallets = pool.newWallets();

    if (!newWallets) {
        cserror() << kLogPrefix << "Pool is read-only";
        return false;
    }

    newWallets->clear();

    std::map<csdb::Address, std::pair<WalletId, csdb::Pool::NewWalletInfo::AddressId>> addrsAndIds;

    csdb::Pool::Transactions& transactions = pool.transactions();
    for (size_t idx = 0; idx < transactions.size(); ++idx) {
        addrsAndIds[transactions[idx].source()].second = {idx, csdb::Pool::NewWalletInfo::AddressType::AddressIsSource};
        addrsAndIds[transactions[idx].target()].second = {idx, csdb::Pool::NewWalletInfo::AddressType::AddressIsTarget};
    }

    const auto& confidants = pool.confidants();
    size_t confWalletsIndexStart = transactions.size();
    for (size_t i = 0; i < confidants.size(); ++i) {
        addrsAndIds[csdb::Address::from_public_key(confidants[i])].second = {confWalletsIndexStart + i, csdb::Pool::NewWalletInfo::AddressType::AddressIsTarget};
    }

    addrsAndIds.erase(genesisAddress_);

    {
        std::lock_guard lock(cacheMutex_);
        walletIds_->normal().fillIds(addrsAndIds);
    }

    for (auto& addrAndId : addrsAndIds) {
        if (!addrAndId.first.is_public_key() || addrAndId.second.first == WalletsIds::kWrongWalletId || addrAndId.first == genesisAddress_) {
            continue;
        }
        newWallets->emplace_back(csdb::Pool::NewWalletInfo{addrAndId.second.second, addrAndId.second.first});
    }
    return true;
}

void BlockChain::tryFlushDeferredBlock() {
    cs::Lock lock(dbLock_);
    if (deferredBlock_.is_valid() && deferredBlock_.is_read_only()) {
        Hash tempHash;
        auto hash = deferredBlock_.hash().to_binary();
        std::copy(hash.cbegin(), hash.cend(), tempHash.data());
        auto mask = cs::Utils::bitsToMask(deferredBlock_.numberTrusted(), deferredBlock_.realTrusted());
        if (NodeUtils::checkGroupSignature(deferredBlock_.confidants(), mask, deferredBlock_.signatures(), tempHash)) {
            deferredBlock_.set_storage(storage_);
            if (deferredBlock_.save()) {
#ifdef DBSQL
                dbsql::saveConfidants(deferredBlock_.sequence(), deferredBlock_.confidants(), deferredBlock_.realTrusted());
#endif
                csdebug() << kLogPrefix << "block #" << WithDelimiters(deferredBlock_.sequence()) << " is flushed to DB";
                deferredBlock_ = csdb::Pool{};
            }
            else {
                cserror() << kLogPrefix << "Failed to flush block #" << WithDelimiters(deferredBlock_.sequence()) << " to DB";
            }
        }
    }
}

void BlockChain::close() {
    stop_ = true;
    tryFlushDeferredBlock();
    cs::Lock lock(dbLock_);
    storage_.close();
    cs::Connector::disconnect(&storage_.readBlockEvent(), this, &BlockChain::onReadFromDB);
    blockHashes_->close();
    trxIndex_->close();

    if (!serializationManPtr_) {
        csinfo() << "Blockchain: no serialization manager provided to save caches for QUICK START.";
        return;
    }

    csinfo() << "Blockchain: try to save caches for QUICK START.";

    if (serializationManPtr_->save()) {
      csinfo() << "Blockchain: caches for QUICK START saved successfully.";
    }
    else {
      csinfo() << "~Blockchain: couldn't save caches for QUICK START.";
    }
}

bool BlockChain::getTransaction(const csdb::Address& addr, const int64_t& innerId, csdb::Transaction& result) const {
    for (auto it = cs::TransactionsIterator(*this, addr); it.isValid(); it.next()) {
        if (it->innerID() == innerId) {
            result = *it;
            return true;
        }
    }
    return false;
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

bool BlockChain::findWalletData(const csdb::Address& address, WalletData& wallData) const {
    if (address.is_wallet_id()) {
        return findWalletData(address.wallet_id(), wallData);
    }

    std::lock_guard lock(cacheMutex_);

    auto wallDataPtr = walletsCacheUpdater_->findWallet(address.public_key());
    if (wallDataPtr) {
        wallData = *wallDataPtr;
        return true;
    }
    return false;
}

bool BlockChain::findWalletData(WalletId id, WalletData& wallData) const {
    std::lock_guard lock(cacheMutex_);
    return findWalletData_Unsafe(id, wallData);
}

bool BlockChain::findWalletData_Unsafe(WalletId id, WalletData& wallData) const {
    auto pubKey = getAddressByType(csdb::Address::from_wallet_id(id), AddressType::PublicKey);
    auto wallDataPtr = walletsCacheUpdater_->findWallet(pubKey.public_key());

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

    cserror() << kLogPrefix << "Wrong address";
    return false;
}

bool BlockChain::findAddrByWalletId(const WalletId id, csdb::Address& addr) const {
    if (!walletIds_->normal().findaddr(id, addr)) {
        return false;
    }

    return true;
}

bool BlockChain::checkForConsistency(csdb::Pool& pool, bool isNew) {
    if (pool.sequence() == 0) {
        return true;
    }
    if (pool.confidants().size() < pool.signatures().size()) {
        return false;
    }
    if (cs::Utils::maskValue(pool.realTrusted()) != pool.signatures().size()) {
        return false;
    }
    csdb::Pool tmp = pool.clone();
    if (isNew && !tmp.compose()) {
        csinfo() << kLogPrefix << "Check for consistency: can't compose block";
        return false;
    }

    cs::Bytes checking = tmp.to_binary();
    csdb::Pool tmpCopy = csdb::Pool::from_binary(std::move(checking));
    if (tmpCopy.sequence() == 0) {
        csinfo() << kLogPrefix << "Check for consistency: Failed to create correct binary representation of block #" << pool.sequence();
        return false;
    }

    if (isNew && tmpCopy.previous_hash() != getLastHash()) {
        csinfo() << kLogPrefix << "Check for consistency: block hash in pool #" << pool.sequence() << " doesn't correspond to the last one";
        return false;
    }
    if (!isNew && tmpCopy.previous_hash() != loadBlock(tmpCopy.sequence() - 1ULL).hash()) {
        csinfo() << kLogPrefix << "Check for consistency: block hash in pool #" << pool.sequence() << " doesn't correspond to the last one";
        return false;
    }

    return true;

}

std::string  BlockChain::printWalletCaches() {
    std::string res;
    csdb::Amount totalCheck{ 0 };
    res += ":\nLast block: " + std::to_string(lastSequence_) + "\n#.     Public Key:                                                    Balance:                    Delegated:  TrxsCount: LastTrxId:  TrxID: Heap:\n";
    int counter = 0;
    iterateOverWallets([&res, &counter, &totalCheck](const cs::PublicKey& addr, const cs::WalletsCache::WalletData& wd) {
        ++counter;
        res += std::to_string(counter) + ". " + cs::Utils::byteStreamToHex(addr.data(), addr.size()) + "   ";
        auto am = wd.balance_.to_string();
        totalCheck += wd.balance_ + wd.delegated_;
        res += am;
        for (size_t k = am.size(); k < 28; ++k) { // 28 positions are covered with " " to align digits
            res += " ";
        }
        auto deleg = wd.delegated_.to_string();
        res += deleg;
        //res += std::to_string(wd.transNum_) + "   ";
        //res += (wd.trxTail_.getLastTransactionId() > 1'000'000'000 ? "No" : std::to_string(wd.trxTail_.getLastTransactionId())) + "   ";
        //res += (wd.lastTransaction_.pool_seq() > 1'000'000'000 ? "No" : std::to_string(wd.lastTransaction_.pool_seq())) + "." + std::to_string(wd.lastTransaction_.index()) + "  ";
        //res += wd.trxTail_.printHeap();
        res += "\n";

        if (wd.delegateSources_ && !wd.delegateSources_->empty()) {
            int delCounter = 0;
            res += "    Delegate Sources(" + std::to_string(wd.delegateSources_->size()) + "):" + "\n";
            for (auto& it : *wd.delegateSources_) {
                ++delCounter;
                res += "        " + std::to_string(counter) + "." + std::to_string(delCounter) + " " + cs::Utils::byteStreamToHex(it.first.data(), it.first.size());
                int cnt = 0;
                for (auto& itt : it.second) {
                    if (cnt > 0) {
                        res += "                                                                            ";
                    }
                    res += "                      " + itt.amount.to_string() + "      " + std::to_string(itt.time) + "\n";
                    ++cnt;
                }
            }
        }
        if (wd.delegateTargets_ && !wd.delegateTargets_->empty()) {
            int delCounter = 0;
            res += "    Delegate Targets(" + std::to_string(wd.delegateTargets_->size()) + "):" + "\n";
            for (auto& it : *wd.delegateTargets_) {
                ++delCounter;
                res += "        " + std::to_string(counter) + "." + std::to_string(delCounter) + " " + cs::Utils::byteStreamToHex(it.first.data(), it.first.size());
                int cnt = 0;
                for (auto& itt : it.second) {
                    if (cnt > 0) {
                        res += "                                                                            ";
                    }
                    res += "                      " + itt.amount.to_string() + "      " + std::to_string(itt.time) + "\n";
                    ++cnt;
                }
            }
        }
        return true;

    });
    res += "---------------------------------------------------------\n";
    res += "Total: " + totalCheck.to_string();
    //csdebug() << res;
    return res;
}

std::optional<csdb::Pool> BlockChain::recordBlock(csdb::Pool& pool, bool isTrusted) {
    const auto last_seq = getLastSeq();
    const auto pool_seq = pool.sequence();

    csdebug() << kLogPrefix << "finish & store block #" << pool_seq << " to chain";

    if (last_seq + 1 != pool_seq) {
        cserror() << kLogPrefix << "cannot record block #" << pool_seq << " to chain, last sequence " << last_seq;
        return std::nullopt;
    }

    pool.set_previous_hash(getLastHash());
    if (!checkForConsistency(pool, true)) {
        csdebug() << kLogPrefix << "Pool #" << pool_seq << " failed the consistency check";
        return std::nullopt;
    }
    if (!checkForConsistency(deferredBlock_, false)) {
        csdebug() << kLogPrefix << "Pool #" << deferredBlock_.sequence() << " failed the consistency check";
        //emit stopNode(true);
        return std::nullopt;
    }

    constexpr cs::Sequence NoSequence = std::numeric_limits<cs::Sequence>::max();
    cs::Sequence flushed_block_seq = NoSequence;

    //if the block is not applied here, but the deferred block is already saved 
//we have situation when we try to save the deferred block anther time
//the pool counter was not incremented and we have to save this block again
    cs::PublicKeys lastConfidants;
    if (pool_seq > 1) {

        cs::Lock lock(dbLock_);

        if (deferredBlock_.sequence() + 1 == pool_seq) {
            lastConfidants = deferredBlock_.confidants();
        }
        else {
            lastConfidants = loadBlock(pool_seq - 1).confidants();
        }
    }

    if (finalizeBlock(pool, isTrusted, lastConfidants)) {
        csdebug() << kLogPrefix << "The block is correct";
        if (!applyBlockToCaches(pool)) {
            csdebug() << kLogPrefix << "failed to apply block to caches";
            return std::nullopt;
        }
    }
    else {
        csdebug() << kLogPrefix << "the signatures of the block are insufficient or incorrect";
        setBlocksToBeRemoved(1U);
        return std::nullopt;
    }
    //========================================

    {
        cs::Lock lock(dbLock_);

        if (deferredBlock_.is_valid()) {

            deferredBlock_.set_storage(storage_);

            if (deferredBlock_.save()) {
#ifdef DBSQL
                dbsql::saveConfidants(pool_seq, deferredBlock_.confidants(), deferredBlock_.realTrusted());
#endif
                flushed_block_seq = deferredBlock_.sequence();
                if (uuid_ == 0 && flushed_block_seq == 1) {
                    uuid_ = uuidFromBlock(deferredBlock_);
                    csdebug() << kLogPrefix << "UUID = " << uuid_;
                }
            }
            else {
                csmeta(cserror) << kLogPrefix << "Couldn't save block: " << deferredBlock_.sequence();
                return std::nullopt;
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

        deferredBlock_ = pool;
        pool = deferredBlock_.clone();
        lastSequence_ = deferredBlock_.sequence();
    }

    //csdetails() << kLogPrefix << "Pool #" << deferredBlock_.sequence() << ": " << cs::Utils::byteStreamToHex(deferredBlock_.to_binary().data(), deferredBlock_.to_binary().size());
    emit storeBlockEvent(pool);
    if constexpr (false && (pool.transactions_count() > 0 || pool.sequence() % 10 == 0)) {//log code
        std::string res = printWalletCaches() + "\nTransactions: \n";
        csdb::Amount r_cost{ 0 };
        for (auto it : pool.transactions()) {
            res += it.id().to_string() + " " + it.source().to_string() + " -> " + it.target().to_string() + " : " + it.amount().to_string() 
                + ", Counted fee: " + std::to_string(it.counted_fee().to_double()) + ", Max fee: " + std::to_string(it.max_fee().to_double()) + "\n";
            r_cost += it.counted_fee().to_double();
        }
        res += "Round cost: " + pool.roundCost().to_string() + " Counted Round cost: " + r_cost.to_string();
        csdebug() << res;
    }

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
    csdebug() << kLogPrefix << "Starting update last block: check ...";
    //if (deferredBlock_.is_valid()) {
    //  csdebug() << "BLOCKCHAIN> Deferred block is invalid, can't update it";
    //  return false;
    //}
    if (poolFrom.is_read_only()) {
        csdebug() << kLogPrefix << "Deferred block is read_only, be carefull";
        //return false;
    }

    if (poolFrom.sequence() != rPackage.poolMetaInfo().sequenceNumber) {
        csdebug() << kLogPrefix << "Deferred block sequence " << poolFrom.sequence() << " doesn't equal to that in the roundPackage " << rPackage.poolMetaInfo().sequenceNumber << ", can't update it";
        return false;
    }
    if (poolFrom.signatures().size() >= rPackage.poolSignatures().size()) {
        csdebug() << kLogPrefix << "Deferred block has more or the same amount Signatures, than received roundPackage, can't update it";
        return true;
    }
    if (poolFrom.previous_hash() != rPackage.poolMetaInfo().previousHash) {
        csdebug() << kLogPrefix << "Deferred block PREVIOUS HASH doesn't equal to that in the roundPackage, can't update it";
        return false;
    }
    csdebug() << kLogPrefix << "Ok";

    csdb::Pool tmpPool;
    tmpPool.set_sequence(poolFrom.sequence());
    tmpPool.set_previous_hash(poolFrom.previous_hash());
    tmpPool.add_real_trusted(cs::Utils::maskToBits(rPackage.poolMetaInfo().realTrustedMask));
    csdebug() << kLogPrefix << "new mask set to deferred block: " << cs::TrustedMask::toString(rPackage.poolMetaInfo().realTrustedMask);
    tmpPool.add_number_trusted(static_cast<uint8_t>(rPackage.poolMetaInfo().realTrustedMask.size()));
    tmpPool.setRoundCost(poolFrom.roundCost());
    tmpPool.set_confidants(poolFrom.confidants());
    for (auto& it : poolFrom.transactions()) {
        tmpPool.add_transaction(it);
    }
    BlockChain::setTimestamp(tmpPool, rPackage.poolMetaInfo().timestamp);
    for (auto& it : poolFrom.smartSignatures()) {
        tmpPool.add_smart_signature(it);
    }
    csdb::Pool::NewWallets* newWallets = tmpPool.newWallets();
    const csdb::Pool::NewWallets& defWallets = poolFrom.newWallets();
    if (!newWallets) {
        csdebug() << kLogPrefix << "newPool is read-only";
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

/*static*/
void BlockChain::setTimestamp(csdb::Pool& block, const std::string& timestamp) {
    block.add_user_field(BlockChain::kFieldTimestamp, timestamp);
}

// user field "kFieldServiceInfo": [0] - info version, [1] - block flag, == 1 if boostrap block

/*static*/
void BlockChain::setBootstrap(csdb::Pool& block, bool is_bootstrap) {
    if (is_bootstrap) {
        std::string info;
        if (block.user_field_ids().count(BlockChain::kFieldServiceInfo) > 0) {
            info = block.user_field(BlockChain::kFieldServiceInfo).value<std::string>();
        }
        if (info.size() >= 2) {
            if (info[1] == '\001') {
                // already set
                csdetails() << "BlockChain: block #" << block.sequence() << " bootstrap flag has already set";
                return;
            }
        }
        else {
            info.resize(2);
        }
        info[1] = '\001'; // boostrap block
        block.add_user_field(BlockChain::kFieldServiceInfo, info);
        csdebug() << "BlockChain: set block #" << block.sequence() << " bootstrap flag";
    }
    else {
        // clear bootstrap flag if set, otherwise ignore
        if (block.user_field_ids().count(BlockChain::kFieldServiceInfo) > 0) {
            std::string info = block.user_field(BlockChain::kFieldServiceInfo).value<std::string>();
            if (info.size() >= 2) {
                if (info[1] == '\000') {
                    // already unset
                    csdetails() << "BlockChain: block #" << block.sequence() << " bootstrap flag has already unset";
                    return;
                }
                info[1] = '\000'; // non-boostrap block
                block.add_user_field(BlockChain::kFieldServiceInfo, info);
                csdebug() << "BlockChain: clear block #" << block.sequence() << " bootstrap flag";
            }
            else {
                cserror() << "BlockChain: unable to parse block service info, incompatible version";
            }
        }
    }
}

/*static*/
bool BlockChain::isBootstrap(const csdb::Pool& block) {
    if (block.user_field_ids().count(BlockChain::kFieldServiceInfo) > 0) {
        std::string s = block.user_field(BlockChain::kFieldServiceInfo).value<std::string>();
        if (s.size() >= 2 && s[0] == 0) {
            return s[1] == 1;
        }
        else {
            csdebug() << "BlockChain: unable to parse block service info, incompatible version";
        }
    }
    return false;
}

bool BlockChain::deferredBlockExchange(cs::RoundPackage& rPackage, const csdb::Pool& newPool) {

    // final compose and test:
    csdb::Pool tmp_clone = newPool.clone();
    auto tmp = rPackage.poolSignatures();
    tmp_clone.set_signatures(tmp);
    tmp_clone.compose();
    Hash tempHash;
    auto hash = tmp_clone.hash();
    auto bytes = hash.to_binary();
    std::copy(bytes.cbegin(), bytes.cend(), tempHash.data());
    if (NodeUtils::checkGroupSignature(tmp_clone.confidants(), rPackage.poolMetaInfo().realTrustedMask, rPackage.poolSignatures(), tempHash)) {
        csmeta(csdebug) << kLogPrefix << "The number of signatures is sufficient and all of them are OK!";
        if (!checkForConsistency(tmp_clone, true)) {
            csdebug() << kLogPrefix << "Replace the deferred block #" << tmp_clone.sequence() << ": consistency check failed";
            return false;
        }
    }
    else {
        cswarning() << kLogPrefix << "Some of Pool Signatures aren't valid. The pool will not be written to DB. It will be automatically written, when we get proper data";
        return false;
    }


    // update deferred block
    std::lock_guard lock(dbLock_);
    deferredBlock_ = tmp_clone;
    this->blockHashes_->update(deferredBlock_);

    return true;
}

bool BlockChain::isSpecial(const csdb::Transaction& t) {
    if (t.user_field(cs::trx_uf::sp::managing).is_valid()) {
        return true;
    }
    return false;
}

bool BlockChain::storeBlock(csdb::Pool& pool, cs::PoolStoreType type) {
    const auto lastSequence = getLastSeq();
    const auto poolSequence = pool.sequence();
    
    csdebug() << csfunc() << "last #" << lastSequence << ", pool #" << poolSequence;

    if (poolSequence < lastSequence) {
        // ignore
        csdebug() << kLogPrefix << "ignore oudated block #" << poolSequence << ", last written #" << lastSequence;
        // it is not error, so caller code nothing to do with it
        return true;
    }

    if (!BlockChain::isBootstrap(pool)) {
        if ((pool.numberConfirmations() == 0 || pool.roundConfirmations().size() == 0) && pool.sequence() > 1) {
            return false;
        }
    }

    if (poolSequence == lastSequence) {
        csdebug() << kLogPrefix << "poolSequence == lastSequence";
        if (isLastBlockUncertain() && pool.sequence() == uncertainSequence_) {
            cslog() << kLogPrefix << "review replacement for uncertain block " << WithDelimiters(poolSequence);
            if (pool.hash() == desiredHash_) {
                cslog() << kLogPrefix << "replacement candidate has excactly desired hash, compare content of both block versions";

                std::lock_guard lock(dbLock_);

                if (BlockChain::testContentEqual(pool, deferredBlock_)) {
                    deferredBlock_ = pool;
                    resetUncertainState();
                    ++cntUncertainReplaced;
                    csdebug() << kLogPrefix << "get desired last block with the same content, continue with blockchain successfully";
                    return true;
                }
                else {
                    csdebug() << kLogPrefix << "the desired last block has the different content, drop it and remove own last block";
                    removeLastBlock();
                    return false;
                }
            }
            else {
                cslog() << kLogPrefix << "replacement candidate has undesired hash, ignore";
            }
        }

        std::lock_guard lock(dbLock_);

        // ignore
        csdebug() << kLogPrefix << "ignore oudated block #" << poolSequence << ", last written #" << lastSequence;
        // it is not error, so caller code nothing to do with it
        return true;
    }

    if (poolSequence == lastSequence + 1) {
        if (pool.previous_hash() != getLastHash()) {
            csdebug() << "BLOCKCHAIN> new pool\'s prev. hash does not equal to current last hash";
            if (getLastHash().is_empty()) {
                cserror() << kLogPrefix << "own last hash is empty";
            }
            if (pool.previous_hash().is_empty()) {
                cserror() << "BLOCKCHAIN> new pool\'s prev. hash is empty, don\'t write it, do not any harm to our blockchain";
                return false;
            }
            if (compromiseLastBlock(pool.previous_hash())) {
                csdebug() << kLogPrefix << "compromise own last block and cancel store operation";
            }
            else {
                //if (lastSequence + 5ULL < cs::Conveyer::instance().currentRoundNumber()) {
                //    arrangeBlocksInCache();
                //}
                csdebug() << kLogPrefix << "remove own last block and cancel store operation";
                removeLastBlock();
            }
            return false;
        }

        setTransactionsFees(pool, type);
        if (type == cs::PoolStoreType::Created) {
            if (!addNewWalletsToPool(pool)) {
                csdebug() << kLogPrefix << "can't write a block without adding new wallets";
            }
        }

        //validate block to prevent bc from saving invalid instances:
        bool check_failed = false;
        emit tryToStoreBlockEvent(pool, &check_failed);
        if (check_failed) {
            csdebug() << kLogPrefix << "The pool " << pool.sequence() << " is invalid, won't be stored";
            if (lastSequence_ == poolSequence) {
                --lastSequence_;
                csdebug() << kLogPrefix << "Deleting defered block: " << deferredBlock_.sequence();
                deferredBlock_ = csdb::Pool{};
            }
            badBlocks_.push_back(pool.sequence());
            emit alarmBadBlock(pool.sequence());
            return false;
        }

        // write immediately
        if (recordBlock(pool, false).has_value()) {
            csdebug() << kLogPrefix << "block #" << poolSequence << " has recorded to chain successfully";
            // unable to call because stack overflow in case of huge written blocks amount possible:
            // testCachedBlocks();
            blocksToBeRemoved_ = 1;
            resetUncertainState(); // every successful record require the new confirmation of uncertainity
            return true;
        }

        csdebug() << kLogPrefix << "failed to store block #" << poolSequence << " to chain";

        // no need to perform removeLastBlock() as we've updated only wallet ids
        removeWalletsInPoolFromCache(pool);
        //here the problem could arise if deferred the block is saved to db
        if (lastSequence_ == poolSequence) {
            --lastSequence_;
            deferredBlock_ = csdb::Pool{};
        }

        return false;
    }

    cs::Lock lock(cachedBlocksMutex_);

    const auto poolHash = pool.hash();
    if (cachedBlocks_->contains(poolSequence)) {
        csdebug() << kLogPrefix << "ignore duplicated block #" << poolSequence << " in cache";
        // it is not error, so caller code nothing to do with it
        cachedBlockEvent(poolSequence);
        return true;
    }

    // cache block for future recording
    cachedBlocks_->insert(pool, type);

    csdebug() << kLogPrefix << "cache block #" << poolSequence << "("
        << (pool.is_read_only() ? "Read-Only" : "Normal")
        << ") signed by " << pool.signatures().size()
        << " nodes for future (" << cachedBlocks_->size() << " total)";

    cachedBlockEvent(poolSequence);

    // cache always successful
    return true;
}

std::string BlockChain::poolInfo(const csdb::Pool& pool) {
    std::string res = "";
    res += "seq: " + std::to_string(pool.sequence());
    res += ", trxs: " + std::to_string(pool.transactions().size());
    res += ", sigs: " + std::to_string(pool.signatures().size());
    //res += ", hash: " + pool.hash().to_string();
    return res;
}

void BlockChain::testCachedBlocks() {
    csdebug() << kLogPrefix << "test cached blocks";

    cs::Lock lock(cachedBlocksMutex_);

    if (cachedBlocks_->isEmpty()) {
        csdebug() << kLogPrefix << "no cached blocks";
        return;
    }

    auto lastSeq = getLastSeq() + 1;

    // clear unnecessary sequence
    if (cachedBlocks_->minSequence() < lastSeq) {
        csdebug() << kLogPrefix << "Remove outdated blocks up to #" << lastSeq << " from cache";
        cachedBlocks_->remove(cachedBlocks_->minSequence(), lastSeq - 1);
    }

    size_t countStored = 0;
    cs::Sequence fromSeq = lastSeq;

    while (!cachedBlocks_->isEmpty()) {
        if (stop_) {
            // stop requested while handle cached blocks
            return;
        }

        auto firstBlockInCache = cachedBlocks_->minSequence();

        if (firstBlockInCache == lastSeq) {
            // retrieve and use block if it is exactly what we need:
            auto data = cachedBlocks_->pop(firstBlockInCache);

            if (!data.has_value()) {
                cswarning() << "cached blocks returned not valid pool, stop testing cache";
                break;
            }

            if (data.value().pool.is_read_only() && data.value().type == cs::PoolStoreType::Created) {
                cswarning() << "created block from chache is read-only";
            }

            const bool ok = storeBlock(data.value().pool, data.value().type);

            if (!ok) {
                cserror() << kLogPrefix << "Failed to record cached block to chain, drop it & wait to request again";
                break;
            }

            ++countStored;

            if (countStored >= 1000) {
                cslog() << "BLOCKCHAIN> stored " << WithDelimiters(countStored)
                    << " blocks " << WithDelimiters(fromSeq) << " .. " << WithDelimiters(fromSeq + countStored) << " from cache";
                countStored = 0;
                fromSeq = lastSeq + 1;
            }

            lastSeq = getLastSeq() + 1;
        }
        else {
            // stop processing, we have not got required block in cache yet
            csdebug() << kLogPrefix << "Stop store blocks from cache. Next blocks in cache #" << firstBlockInCache;
            break;
        }
    }

    if (countStored > 0) {
        cslog() << "BLOCKCHAIN> stored " << WithDelimiters(countStored)
            << " blocks " << WithDelimiters(fromSeq) << " .. " << WithDelimiters(fromSeq + countStored) << " from cache";
        countStored = 0;
        fromSeq = lastSeq + 1;
    }
}

std::optional<BlockChain::SequenceInterval> BlockChain::getFreeSpaceBlocks() const {
    auto lastWrittenSequence = getLastSeq();
    cs::Sequence sequence = 0;

    {
        cs::Lock lock(cachedBlocksMutex_);

        if (!cachedBlocks_->isEmpty()) {
            sequence = cachedBlocks_->minSequence();
        }
    }

    if (sequence <= lastWrittenSequence || (sequence - lastWrittenSequence) == 0) {
        return std::nullopt;
    }

    return std::make_optional(std::make_pair(lastWrittenSequence + 1, sequence));
}

const cs::ReadBlockSignal& BlockChain::readBlockEvent() const {
    return storage_.readBlockEvent();
}

const cs::StartReadingBlocksSignal& BlockChain::startReadingBlocksEvent() const {
    return storage_.readingStartedEvent();
}

std::size_t BlockChain::getCachedBlocksSize() const {
    cs::Lock lock(cachedBlocksMutex_);
    return cachedBlocks_->size();
}

std::size_t BlockChain::getCachedBlocksSizeSynced() const {
    cs::Lock lock(cachedBlocksMutex_);
    return cachedBlocks_->sizeSynced();
}

void BlockChain::clearBlockCache() {
    cs::Lock lock(cachedBlocksMutex_);
    cachedBlocks_->clear();
}

std::vector<BlockChain::SequenceInterval> BlockChain::getRequiredBlocks() const {
    cs::Sequence seq = getLastSeq();
    const auto firstSequence = seq + 1;
    const auto currentRoundNumber = cs::Conveyer::instance().currentRoundNumber()-1;

    if (firstSequence >= currentRoundNumber) {
        return std::vector<SequenceInterval>();
    }

    const auto roundNumber = currentRoundNumber > 0 ? std::max(firstSequence, currentRoundNumber - 1) : 0;

    // return at least [next, 0] or [next, currentRoundNumber]:
    if (cachedBlocks_->isEmpty()) {
        return std::vector<SequenceInterval>{ {firstSequence, roundNumber} };
    }

    auto ranges = cachedBlocks_->ranges();

    if (ranges.empty()) {
        return std::vector<SequenceInterval>{ {firstSequence, roundNumber} };
    }
    bool emplaceLater = false;
    if (firstSequence < ranges.front().first) {
        ranges.emplace_back(firstSequence, cachedBlocks_->minSequence() - 1);
        emplaceLater = true;
    }
    //TODO: this piece of code should be precisely examined
    if (ranges.back().second < roundNumber) {
        ranges.emplace_back(cachedBlocks_->maxSequence() + 1, roundNumber);
    }
    if (emplaceLater) {
        ranges.emplace_back(firstSequence, cachedBlocks_->minSequence() - 1);
    }


    return ranges;
}

void BlockChain::setTransactionsFees(TransactionsPacket& packet) {
    fee::setCountedFees(packet.transactions());
}

void BlockChain::setTransactionsFees(csdb::Pool& pool, cs::PoolStoreType type) {
    csdebug() << __func__;
    if (pool.is_read_only() && type == cs::PoolStoreType::Created) {
        cserror() << kLogPrefix << "Pool is read-only";
        return;
    }
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
    auto wallDataPtr = walletsCacheUpdater_->findWallet(pubKey.public_key());

    if (!wallDataPtr) {
        return 0;
    }

    return static_cast<uint32_t>(wallDataPtr->transNum_);
}

csdb::TransactionID BlockChain::getLastTransaction(const csdb::Address& addr) const {
    std::lock_guard lock(cacheMutex_);

    auto pubKey = getAddressByType(addr, AddressType::PublicKey);
    auto wallDataPtr = walletsCacheUpdater_->findWallet(pubKey.public_key());

    if (!wallDataPtr) {
        return csdb::TransactionID();
    }

    return wallDataPtr->lastTransaction_;
}

cs::Sequence BlockChain::getPreviousPoolSeq(const csdb::Address& addr, cs::Sequence ps) const {
    auto previousSequence = trxIndex_->getPrevTransBlock(addr, ps);

    if (previousSequence == ps) {
        auto pubKey = getAddressByType(addr, AddressType::PublicKey).public_key();
        cserror() << kLogPrefix << "Inconsistent transaction index for public key: "
                  << EncodeBase58(Bytes(pubKey.begin(), pubKey.end()))
                  << ", block seq is " << ps;

        if (cs::ConfigHolder::instance().config()->autoShutdownEnabled()) {
            cserror() << kLogPrefix << "Node will be stopped due to index error. Please restart it.";

            trxIndex_->invalidate();
            Node::requestStop();
        }

        return kWrongSequence;
    }

    return previousSequence;
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

cs::Sequence BlockChain::getLastSeq() const {
    return lastSequence_;
}

const MultiWallets& BlockChain::multiWallets() const {
    return walletsCacheStorage_->multiWallets();
}

void BlockChain::setBlocksToBeRemoved(cs::Sequence number) {
    if (blocksToBeRemoved_ > 0) {
        csdebug() << kLogPrefix << "Can't change number of blocks to be removed, because the previous removal is still not finished";
        return;
    }
    csdebug() << kLogPrefix << "Allowed NUMBER blocks to remove is set to " << blocksToBeRemoved_;
    blocksToBeRemoved_ = number;
}

namespace {
    static void serializeComparableContent(cs::ODataStream<cs::Bytes>& out, const csdb::Pool& block) {
        out << block.sequence();

        // put user fields except special timestamp field "TimestampID"
        const auto ids = block.user_field_ids();
        size_t cnt = ids.size();
        if (cnt > 0) {
            cnt -= ids.count(BlockChain::kFieldTimestamp);
            if (cnt > 0) {
                for (const auto id: ids) {
                    if (id == BlockChain::kFieldTimestamp) {
                        continue;
                    }
                    out << id;
                    const auto fld = block.user_field(id);
                    if (fld.is_valid()) {
                        switch (fld.type()) {
                        case csdb::UserField::Integer:
                            out << fld.value<int>();
                            break;
                        case csdb::UserField::Amount:
                            out << fld.value<csdb::Amount>();
                            break;
                        case csdb::UserField::String:
                            out << fld.value<std::string>();
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
        }

        out << block.roundCost();

        out << block.transactions_count();
        for (const auto& t : block.transactions()) {
            out << t.to_byte_stream_for_sig();
        }

        const auto& wallets = block.newWallets();
        out << wallets.size();
        for (const auto& w : wallets) {
            union {
                csdb::Pool::NewWalletInfo::AddressId address_id;
                size_t value;
            } conv{};
            conv.address_id = w.addressId_;
            out << conv.value;
            out << w.walletId_;
        }
        const auto& confidants = block.confidants();
        for (const auto& it : confidants) {
            out << it;
        }
        out << block.previous_hash();
    }
}

/*static*/
bool BlockChain::testContentEqual(const csdb::Pool& lhs, const csdb::Pool& rhs) {
    if (lhs.is_valid() != rhs.is_valid()) {
        return false;
    }
    if (!lhs.is_valid()) {
        // both has no content
        return true;
    }

    cs::Bytes lhs_bin;
    cs::ODataStream lhs_out(lhs_bin);
    serializeComparableContent(lhs_out, lhs);
    const cs::Hash l = cscrypto::calculateHash(lhs_bin.data(), lhs_bin.size());

    cs::Bytes rhs_bin;
    cs::ODataStream rhs_out(rhs_bin);
    serializeComparableContent(rhs_out, rhs);
    const cs::Hash r = cscrypto::calculateHash(rhs_bin.data(), rhs_bin.size());

    return std::equal(l.cbegin(), l.cend(), r.cbegin());
}

void BlockChain::addIncorrectBlockNumber(cs::Sequence seq) {
    incorrectBlocks_.push_back(seq);
}

std::vector<cs::Sequence>* BlockChain::getIncorrectBlockNumbers() {
    return &incorrectBlocks_;
}

void BlockChain::showDBParams() {
    csinfo() << "last seq = " << lastSequence_ << ", db size = " << storage_.size();
}
//while caching the blocks are put out of the storeage in the with sequence decrement
void BlockChain::cacheLastBlocks() {
    csinfo() << kLogPrefix << __func__;//we have to begin with good block
    bool firstIteration = false;
    if (!antiForkMode_) {
        antiForkMode_ = true;
        firstIteration = true;
    }

    cs::Sequence lastSeq;
    while (!incorrectBlocks_.empty() || !selectionFinished_) {
        csinfo() << kLogPrefix << "Starting blocks transferring cycle";
        auto lastBlock = getLastBlock();
        lastSeq = lastBlock.sequence();
        csinfo() << kLogPrefix << "now dealing with " << lastSeq;

        if (incorrectBlocks_.back() < lastBlock.sequence()) {
            csinfo() << kLogPrefix << "incorrect block sequence not reached";//and selFin == " << (selectionFinished_?"true":"false");
        }
        else if (incorrectBlocks_.back() == lastSeq) {
            csinfo() << kLogPrefix << "Incorrect block reached, remove it form list";
            incorrectBlocks_.pop_back();
        }
        else {
            csinfo() << kLogPrefix << "Incorrect block overjumped - hmm .. look though your code better";
        }

        if (lastBlock.is_valid() && lastBlock.hash() == lastPrevHash_ || firstIteration) {
            selectionFinished_ = true;
            csinfo() << kLogPrefix << "caching block " << lastBlock.sequence();
            cachedBlocks_->insert(lastBlock, cs::PoolStoreType::Restored);
        }
        else {
            emittingRequest_ = 1;
            selectionFinished_ = false;
            csinfo() << kLogPrefix << "reached block with nonconsistant hash: orderNecessaryBlock";
            emit orderNecessaryBlock(lastPrevHash_, lastSeq);
            blocksToBeRemoved_ = 1;
            removeLastBlock();
            return;
        }

        lastPrevHash_ = lastBlock.previous_hash();
        blocksToBeRemoved_ = 1;
        removeLastBlock();
    }
    antiForkMode_ = false;
    lastPrevHash_ = csdb::PoolHash();
 }

void BlockChain::replaceCachedIncorrectBlock(const csdb::Pool& block) {
    csdebug() << kLogPrefix << __func__;
    lastPrevHash_ = block.previous_hash();
    cachedBlocks_->insert(block, cs::PoolStoreType::Synced);
    if (emittingRequest_ == 1) {
        emittingRequest_ = 0;
        cacheLastBlocks();
    }
    if (emittingRequest_ == 2) {
        emittingRequest_ = 0;
        arrangeBlocksInCache();
    }
    if (emittingRequest_ == 3) {
        emittingRequest_ = 0;
        badBlockIssue(block);
    }
}

void BlockChain::arrangeBlocksInCache() {
    csdebug() << kLogPrefix << __func__;
    if (!antiForkMode_ && cachedBlocks_->maxSequence() < getLastSeq() + 5ULL) {
        return;
    }
    if (neededCacheSeq_ == 0ULL) {
        antiForkMode_ = true;
        neededCacheSeq_ = getLastSeq() + 4ULL;
        startingBchSeq_ = getLastSeq();
    }

    while (neededCacheSeq_ > getLastSeq() || lastPrevHash_ != getLastHash()) {
        if (cachedBlocks_->contains(neededCacheSeq_)) {
            auto data = cachedBlocks_->value(neededCacheSeq_);
            csdb::Pool currentCachedBlock;
            if (data.has_value()) {
                currentCachedBlock = data.value().pool;
            }
            else {
                //TODO - think how to find the first synched block in cache !!!
                csdebug() << "No valid synced block found in cache trying to order it: " << neededCacheSeq_;
                emittingRequest_ = 2;
                emit orderNecessaryBlock(lastPrevHash_, neededCacheSeq_);
                return;
            }
            lastPrevHash_ = currentCachedBlock.previous_hash();
        }
        else {
            csdebug() << "No synced block found in cache trying to order it :" << neededCacheSeq_;
            emittingRequest_ = 2;
            emit orderNecessaryBlock(lastPrevHash_, neededCacheSeq_);
            return;
        }

        
        --neededCacheSeq_;
    }

    antiForkMode_ = false;
    lastPrevHash_ = csdb::PoolHash();
    neededCacheSeq_ = 0ULL;
    startingBchSeq_ = 0ULL;
}

void BlockChain::lookForBadBlocks() {
    if (badBlocks_.empty()) {
        return;
    }
    //TODO: explore bad blocks
    //for (auto it : badBlocks_) {

    //}
}

void BlockChain::badBlockIssue(const csdb::Pool& pool) {
    csdebug() << kLogPrefix << __func__ << ": "<<  pool.sequence();
    if (!antiForkMode_ && cachedBlocks_->maxSequence() < getLastSeq() + 5ULL) {
        csdebug() << kLogPrefix << "AntiForkMode is " << (antiForkMode_?"ON":"OFF") << ", cached blocks: "
            << cachedBlocks_->maxSequence() << ", last seq: " << getLastSeq();
        return;
    }
    cachedBlocks_->insert(pool, cs::PoolStoreType::Synced);
    if (neededCacheSeq_ == 0ULL) {
        csdebug() << kLogPrefix << "Initializing variables";
        antiForkMode_ = true;
        lastPrevHash_ = getLastHash();
        startingBchSeq_ = getLastSeq();
        neededCacheSeq_ = startingBchSeq_;
    }
    while (lastPrevHash_ == pool.previous_hash()) {
        if (cachedBlocks_->contains(neededCacheSeq_) && cachedBlocks_->value(neededCacheSeq_).has_value()) {
            csdb::Pool currentBlock = cachedBlocks_->value(neededCacheSeq_).value().pool;
            if (cachedBlocks_->maxSequence() > neededCacheSeq_ && cachedBlocks_->contains(++neededCacheSeq_)) {
                lastPrevHash_ = currentBlock.hash();
            }
        }

        else {
            emittingRequest_ = 3;
            emit orderNecessaryBlock(lastPrevHash_, neededCacheSeq_);
            return;
        }

    }
    arrangeBlocksInCache();
}

void BlockChain::getCachedMissedBlock(const csdb::Pool& block) {
    csdebug() << kLogPrefix << __func__;
    lastPrevHash_ = block.previous_hash();
    cachedBlocks_->insert(block, cs::PoolStoreType::Synced);
    cacheLastBlocks();
}
