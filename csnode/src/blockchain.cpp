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
#include <csnode/node.hpp>
#include <csnode/transactionsindex.hpp>
#include <csnode/transactionsiterator.hpp>
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
, multiWallets_(new MultiWallets())
, cacheMutex_() {
    createCachesPath();

    walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();
    blockHashes_ = std::make_unique<cs::BlockHashes>(cachesPath);
    trxIndex_ = std::make_unique<cs::TransactionsIndex>(*this, cachesPath, recreateIndex);

    cs::Connector::connect(&storage_.readingStartedEvent(), trxIndex_.get(), &TransactionsIndex::onStartReadFromDb);
    cs::Connector::connect(&storage_.readingStartedEvent(), this, &BlockChain::onStartReadFromDB);

    // the order of two following calls matters
    cs::Connector::connect(&storage_.readBlockEvent(), trxIndex_.get(), &TransactionsIndex::onReadFromDb);
    cs::Connector::connect(&storage_.readBlockEvent(), this, &BlockChain::onReadFromDB);

    cs::Connector::connect(&storage_.readingStoppedEvent(), trxIndex_.get(), &TransactionsIndex::onDbReadFinished);
    cs::Connector::connect(&storage_.readingStoppedEvent(), walletsCacheUpdater_.get(), &WalletsCache::Updater::onStopReadingFromDB);

#ifdef MONITOR_NODE
    cs::Connector::connect(&walletsCacheUpdater_->updateFromDBFinishedEvent, multiWallets_.get(), &MultiWallets::onDbReadFinished);
    cs::Connector::connect(&walletsCacheUpdater_->updateFromDBFinishedEvent, [this] (const auto&) {
        cs::Connector::connect(&walletsCacheUpdater_->walletUpdateEvent, multiWallets_.get(), &MultiWallets::onWalletCacheUpdated);
    });
#endif
}

BlockChain::~BlockChain() {}

bool BlockChain::init(const std::string& path, cs::Sequence newBlockchainTop) {
    cs::Connector::connect(&this->removeBlockEvent, trxIndex_.get(), &TransactionsIndex::onRemoveBlock);

    cslog() << kLogPrefix << "Trying to open DB...";

    size_t totalLoaded = 0;
    lastSequence_ = 0;
    csdb::Storage::OpenCallback progress = [&](const csdb::Storage::OpenProgress& progress) {
        ++totalLoaded;
        if (progress.poolsProcessed % 1000 == 0) {
            std::cout << '\r' << WithDelimiters(progress.poolsProcessed) << std::flush;
        }
        return false;
    };

    if (!storage_.open(path, progress, newBlockchainTop)) {
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
        writeGenesisBlock();
    }
    else {
        if (!postInitFromDB()) {
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
    cs::Lock lock(dbLock_);
    return uuid_;
}

void BlockChain::onStartReadFromDB(cs::Sequence lastWrittenPoolSeq) {
    if (lastWrittenPoolSeq > 0) {
        cslog() << kLogPrefix << "start reading " << WithDelimiters(lastWrittenPoolSeq + 1)
            << " blocks from DB, 0.." << WithDelimiters(lastWrittenPoolSeq);
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
        updateNonEmptyBlocks(block);
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
            csdebug() << kLogPrefix << "Wallet with negative balance (" << bal << ") detected: "
                      << cs::Utils::byteStreamToHex(key.data(), key.size()) << " ("
                      << EncodeBase58(key.data(), key.data() + key.size()) << ")";
        }
        return true;
    };
    walletsCacheStorage_->iterateOverWallets(func);
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
        if (deferredBlock_.user_field_ids().count(0) > 0) {
            return deferredBlock_.user_field(0).value<std::string>();
        }
        else {
            return std::string("0");
        }
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
			cserror() << kLogPrefix << "storage is corrupted, storage rescan is required";
			return;
		}

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
			cswarning() << kLogPrefix << "Hashes cache is corrupted, storage rescan is required";
			remove_hash = pool.hash();
		}

		if (pool.sequence() == 0) {
			csmeta(cswarning) << kLogPrefix << "Attempt to remove Genesis block !!!!!";
			return;
		}

		// such operations are only possible on valid pool:
		total_transactions_count_ -= pool.transactions().size();
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

void BlockChain::updateLastTransactions(const std::vector<std::pair<cs::PublicKey, csdb::TransactionID>>& updates) {
    std::lock_guard l(cacheMutex_);
    walletsCacheUpdater_->updateLastTransactions(updates);
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
                cswarning() << kLogPrefix << "Wallet was not removed";
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
        csmeta(cserror) << kLogPrefix << "Couldn't compose block: " << pool.sequence();
        return false;
    }

    cs::Sequence currentSequence = pool.sequence();
    const auto& confidants = pool.confidants();
    const auto& signatures = pool.signatures();
    const auto& realTrusted = pool.realTrusted();
    if (currentSequence > 1) {
        csdebug() << kLogPrefix << "Finalize: starting confidants validation procedure:";

        cs::Bytes trustedToHash;
        cs::DataStream tth(trustedToHash);
        tth << currentSequence;
        tth << confidants;

        cs::Hash trustedHash = cscrypto::calculateHash(trustedToHash.data(), trustedToHash.size());

        cs::Signatures sigs = pool.roundConfirmations();
        const auto& confMask = cs::Utils::bitsToMask(pool.numberConfirmations(), pool.roundConfirmationMask());
        // for debugging only delete->
        csdebug() << kLogPrefix << "Mask size = " << confMask.size() << " for next confidants:";
        for (auto& it : lastConfidants) {
            csdebug() << cs::Utils::byteStreamToHex(it.data(), it.size());
        }
        // <-delete
        if (confMask.size() > 1) {
            if (!NodeUtils::checkGroupSignature(lastConfidants, confMask, sigs, trustedHash)) {
                csdebug() << kLogPrefix << "           The Confidants confirmations are not OK";
                return false;
            }
            else {
                csdebug() << kLogPrefix << "           The Confidants confirmations are OK";
            }
        }
        else {
            // TODO: add SS PKey to the prevConfidants
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
            csmeta(csdebug) << kLogPrefix << "The number of signatures is sufficient and all of them are OK!";
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

    trxIndex_->update(pool);
    updateNonEmptyBlocks(pool);

    if (!updateFromNextBlock(pool)) {
        csmeta(cserror) << kLogPrefix << "Error in updateFromNextBlock()";
        return false;
    }

    csmeta(csdetails) << kLogPrefix << "last hash: " << pool.hash().to_string();
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
        cserror() << kLogPrefix << "Pool is read-only";
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
    tryFlushDeferredBlock();
    cs::Lock lock(dbLock_);
    storage_.close();
    cs::Connector::disconnect(&storage_.readBlockEvent(), this, &BlockChain::onReadFromDB);
    blockHashes_->close();
    trxIndex_->close();
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
        if (!blockHashes_->onNextBlock(nextPool)) {
            cslog() << kLogPrefix << "Error writing DB structure";
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

bool BlockChain::findWalletData(const csdb::Address& address, WalletData& wallData) const {
    if (address.is_wallet_id()) {
        return findWalletData(address.wallet_id(), wallData);
    }

    std::lock_guard lock(cacheMutex_);

    const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(address.public_key());
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

    cserror() << kLogPrefix << "Wrong address";
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

    cserror() << kLogPrefix << "Wrong address";
    return false;
}

bool BlockChain::findAddrByWalletId(const WalletId id, csdb::Address& addr) const {
    if (!walletIds_->normal().findaddr(id, addr)) {
        return false;
    }

    return true;
}

bool BlockChain::checkForConsistency(csdb::Pool& pool) {
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
    if (!tmp.compose()) {
        return false;
    }
    cs::Bytes checking = tmp.to_binary();
    csdb::Pool tmpCopy = csdb::Pool::from_binary(std::move(checking));
    if (tmpCopy.sequence() == 0) {
        csinfo() << kLogPrefix << "Failed to create correct binary representation of block #" << pool.sequence();
        return false;
    }
    return true;

}

void  BlockChain::printWalletCaches() {
    std::string res;
    csdb::Amount totalCheck{ 0 };
    res += ":\n#.     Public Key:                                                    Balance:                    Delegated:\n";
    int counter = 0;
    iterateOverWallets([&res, &counter, &totalCheck](const cs::PublicKey& addr, const cs::WalletsCache::WalletData& wd) {
        ++counter;
        res += std::to_string(counter) + ". " + cs::Utils::byteStreamToHex(addr.data(), addr.size()) + "   ";
        auto am = wd.balance_.to_string();
        totalCheck += wd.balance_;
        res += am;
        for (int k = am.size(); k < 28; ++k) { // 28 positiona are covered with " " to align digits
            res += " ";
        }
        res += wd.delegated_.to_string() + "\n";
        if (!wd.delegats_.empty()) {
            int delCounter = 0;
            res += "    Delegats(" + std::to_string(wd.delegats_.size()) + "):" + "\n";
            for (auto& it : wd.delegats_) {
                ++delCounter;
                res += "        " + std::to_string(counter) + "." + std::to_string(delCounter) + " " + cs::Utils::byteStreamToHex(it.first.data(), it.first.size());
                res += "    " + it.second.to_string() + "\n";

            }
        }
        return true;

    });
    res += "---------------------------------------------------------\n";
    res += "Total: " + totalCheck.to_string();
    csdebug() << res;
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
    if (!checkForConsistency(pool)) {
        csdebug() << kLogPrefix << "Pool #" << pool_seq << " failed the consistency check";
        return std::nullopt;
    }

    if (!checkForConsistency(deferredBlock_)) {
        csdebug() << kLogPrefix << "Deferred Block #" << deferredBlock_.sequence() << " failed the consistency check";
        return std::nullopt;
    }
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
                    csdebug() << kLogPrefix << "UUID = " << uuid_;
                }
            }
            else {
                csmeta(cserror) << kLogPrefix << "Couldn't save block: " << deferredBlock_.sequence();
            }
        }
    }

    if (flushed_block_seq != NoSequence) {
        csdebug() << "---------------------------- Flush block #" << flushed_block_seq << " to disk ---------------------------";
        csdebug() << "signatures amount = " << deferredBlock_.signatures().size() << ", smartSignatures amount = " << deferredBlock_.smartSignatures().size()
                  << ", see block info above";
        csdebug() << "----------------------------------------------------------------------------------";
    }

    //printWalletCaches();

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
            csdebug() << kLogPrefix << "The block is correct";
        }
        else {
            csdebug() << kLogPrefix << "the signatures of the block are incorrect";
            setBlocksToBeRemoved(1U);
            return std::nullopt;
        }
        pool = deferredBlock_.clone();
    }
    csdetails() << kLogPrefix << "Pool #" << deferredBlock_.sequence() << ": " << cs::Utils::byteStreamToHex(deferredBlock_.to_binary().data(), deferredBlock_.to_binary().size());
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
    tmpPool.add_user_field(0, rPackage.poolMetaInfo().timestamp);
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

bool BlockChain::deferredBlockExchange(cs::RoundPackage& rPackage, const csdb::Pool& newPool) {
    deferredBlock_ = csdb::Pool{};
    deferredBlock_ = newPool;
    auto tmp = rPackage.poolSignatures();
    deferredBlock_.set_signatures(tmp);
    deferredBlock_.compose();
    Hash tempHash;
    auto hash = deferredBlock_.hash();
    this->blockHashes_->update(deferredBlock_);
    auto bytes = hash.to_binary();
    std::copy(bytes.cbegin(), bytes.cend(), tempHash.data());
    if (NodeUtils::checkGroupSignature(deferredBlock_.confidants(), rPackage.poolMetaInfo().realTrustedMask, rPackage.poolSignatures(), tempHash)) {
        csmeta(csdebug) << kLogPrefix << "The number of signatures is sufficient and all of them are OK!";

    }
    else {
        cswarning() << kLogPrefix << "Some of Pool Signatures aren't valid. The pool will not be written to DB. It will be automatically written, when we get proper data";
        return false;
    }
    return true;
}

bool BlockChain::storeBlock(csdb::Pool& pool, bool bySync) {
    const auto lastSequence = getLastSeq();
    const auto poolSequence = pool.sequence();
    csdebug() << csfunc() << "last #" << lastSequence << ", pool #" << poolSequence;

    if (poolSequence <= lastSequence) {
        // ignore
        csdebug() << kLogPrefix << "ignore oudated block #" << poolSequence << ", last written #" << lastSequence;
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
            csdebug() << kLogPrefix << "ignore oudated block #" << poolSequence << ", last written #" << lastSequence;
            // it is not error, so caller code nothing to do with it
            return true;
        }
        else {
            csdebug() << kLogPrefix << "we have to rewrite #" << poolSequence;
            // removeLastBlock();
        }
    }

    if (poolSequence == lastSequence + 1) {
        if (pool.previous_hash() != getLastHash()) {
			csdebug() << kLogPrefix << "new pool\'s prev. hash does not equal to current last hash";
            if (getLastHash().is_empty()) {
                cserror() << kLogPrefix << "own last hash is empty";
            }
            if (pool.previous_hash().is_empty()) {
                cserror() << kLogPrefix << "new pool\'s prev. hash is empty, don\'t write it, do not any harm to our blockchain";
				return false;
            }
			csdebug() << kLogPrefix << "remove own last block and cancel store operation";
            removeLastBlock();
            return false;
        }


        setTransactionsFees(pool);

        //validate block to prevent bc from saving invalid instances:
        bool check_failed = false;
        emit tryToStoreBlockEvent(pool, &check_failed);
        if (check_failed) {
            csdebug() << kLogPrefix << "The pool " << pool.sequence() << " is invalid, won't be stored";
            return false;
        }
        // update wallet ids
        // it should be done before check pool's signature,
        // because it can change pool's binary representation
        if (bySync) {
            // ready-to-record block does not require anything
            csdebug() << kLogPrefix << "store block #" << poolSequence << " to chain, update wallets ids";
            updateWalletIds(pool, *walletsCacheUpdater_);
        }
        else {
            csdebug() << kLogPrefix << "store block #" << poolSequence << " add new wallets to pool";
            addNewWalletsToPool(pool);
        }


        // write immediately
        if (recordBlock(pool, false).has_value()) {
            csdebug() << kLogPrefix << "block #" << poolSequence << " has recorded to chain successfully";
            // unable to call because stack overflow in case of huge written blocks amount possible:
            // testCachedBlocks();
			blocksToBeRemoved_ = 1;
            return true;
        }

        csdebug() << kLogPrefix << "failed to store block #" << poolSequence << " to chain";

        // no need to perform removeLastBlock() as we've updated only wallet ids
        removeWalletsInPoolFromCache(pool);

        if (lastSequence_ == poolSequence) {
            --lastSequence_;
            deferredBlock_ = csdb::Pool{};
        }

        return false;
    }

    if (cachedBlocks_.count(poolSequence) > 0) {
        csdebug() << kLogPrefix << "ignore duplicated block #" << poolSequence << " in cache";
        // it is not error, so caller code nothing to do with it
        cachedBlockEvent(poolSequence);
        return true;
    }
    // cache block for future recording
    cachedBlocks_.emplace(poolSequence, BlockMeta{pool, bySync});
    csdebug() << kLogPrefix << "cache block #" << poolSequence << " signed by " << pool.signatures().size()
        << " nodes for future (" << cachedBlocks_.size() << " total)";
    cachedBlockEvent(poolSequence);
    // cache always successful
    return true;
}

void BlockChain::testCachedBlocks() {
    csdebug() << kLogPrefix << "test cached blocks";
    if (cachedBlocks_.empty()) {
        csdebug() << kLogPrefix << "no cached blocks";
        return;
    }

    auto lastSeq = getLastSeq() + 1;
    // clear unnecessary sequence
    if (cachedBlocks_.cbegin()->first < lastSeq) {
        auto it = cachedBlocks_.lower_bound(lastSeq);
        if (it != cachedBlocks_.begin()) {
            csdebug() << kLogPrefix << "Remove outdated blocks up to #" << (*it).first << " from cache";
            cachedBlocks_.erase(cachedBlocks_.begin(), it);
        }
    }

    size_t cnt_stored = 0;
    cs::Sequence fromSeq = lastSeq;
    while (!cachedBlocks_.empty()) {
        auto firstBlockInCache = cachedBlocks_.begin();

        if ((*firstBlockInCache).first == lastSeq) {
            // retrieve and use block if it is exactly what we need:
            auto data = (*firstBlockInCache).second;
            const bool ok = storeBlock(data.pool, data.by_sync);
            cachedBlocks_.erase(firstBlockInCache);
            if (!ok) {
                cserror() << kLogPrefix << "Failed to record cached block to chain, drop it & wait to request again";
                break;
            }
            ++cnt_stored;
            if (cnt_stored >= 1000) {
                cslog() << kLogPrefix << "stored " << WithDelimiters(cnt_stored)
                    << " blocks " << WithDelimiters(fromSeq) << " .. " << WithDelimiters(fromSeq + cnt_stored) << " from cache";
                cnt_stored = 0;
                fromSeq = lastSeq + 1;
            }
            ++lastSeq;
        }
        else {
            // stop processing, we have not got required block in cache yet
            csdebug() << kLogPrefix << "Stop store blocks from cache. Next blocks in cache #" << (*firstBlockInCache).first;
            break;
        }
    }
    if (cnt_stored > 0) {
        cslog() << "BLOCKCHAIN> stored " << WithDelimiters(cnt_stored)
            << " blocks " << WithDelimiters(fromSeq) << " .. " << WithDelimiters(fromSeq + cnt_stored) << " from cache";
        cnt_stored = 0;
        fromSeq = lastSeq + 1;
    }
}

const cs::ReadBlockSignal& BlockChain::readBlockEvent() const {
    return storage_.readBlockEvent();
}

const cs::StartReadingBlocksSignal& BlockChain::startReadingBlocksEvent() const {
    return storage_.readingStartedEvent();
}

std::size_t BlockChain::getCachedBlocksSize() const {
    return cachedBlocks_.size();
}

void BlockChain::clearBlockCache() {
	cachedBlocks_.clear();
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
    auto prev_seq = trxIndex_->getPrevTransBlock(addr, ps);

    if (prev_seq == ps) {
        auto pubKey = getAddressByType(addr, AddressType::PublicKey).public_key();
        cserror() << kLogPrefix << "Inconsistent transaction index for public key: "
                  << EncodeBase58(Bytes(pubKey.begin(), pubKey.end()))
                  << ", block seq is " << ps;

        if (Node::autoShutdownEnabled()) {
            cserror() << kLogPrefix << "Node will be stopped due to index error. Please restart it.";

            trxIndex_->invalidate();
            Node::requestStop();
        }
        return kWrongSequence;
    }

    return prev_seq;
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
    return *(multiWallets_.get());
}

void BlockChain::setBlocksToBeRemoved(cs::Sequence number) {
    if (blocksToBeRemoved_ > 0) {
        csdebug() << kLogPrefix << "Can't change number of blocks to be removed, because the previous removal is still not finished";
        return;
    }
    csdebug() << kLogPrefix << "Allowed NUMBER blocks to remove is set to " << blocksToBeRemoved_;
    blocksToBeRemoved_ = number;
}
