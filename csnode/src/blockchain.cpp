#include <base58.h>
#include <csdb/currency.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <limits>

#include <csnode/blockchain.hpp>
#include <csnode/blockhashes.hpp>
#include <csnode/blockvalidator.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>
#include <csnode/fee.hpp>
#include <csnode/nodeutils.hpp>
#include <solver/smartcontracts.hpp>

#include <client/config.hpp>

//#define RECREATE_INDEX

using namespace cs;

BlockChain::BlockChain(csdb::Address genesisAddress, csdb::Address startAddress)
: good_(false)
, dbLock_()
, genesisAddress_(genesisAddress)
, startAddress_(startAddress)
, walletIds_(new WalletsIds)
, walletsCacheStorage_(new WalletsCache(WalletsCache::Config(), genesisAddress, startAddress, *walletIds_))
, walletsPools_(new WalletsPools(genesisAddress, startAddress, *walletIds_))
, cacheMutex_()
, blockValidator_(std::make_unique<cs::BlockValidator>(*this)) {
    cs::Connector::connect(&storage_.readBlockEvent(), this, &BlockChain::onReadFromDB);
    walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();
    blockHashes_ = std::make_unique<cs::BlockHashes>();
}

BlockChain::~BlockChain() {
}

bool BlockChain::init(const std::string& path) {
    cslog() << "Trying to open DB...";

    size_t totalLoaded = 0;
    csdb::Storage::OpenCallback progress = [&](const csdb::Storage::OpenProgress& progress) {
        ++totalLoaded;
        if (progress.poolsProcessed % 1000 == 0) {
            std::cout << '\r' << WithDelimiters(progress.poolsProcessed) << "";
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
        std::cout << "Done\n";
    }

#if defined(TRANSACTIONS_INDEX) && defined(RECREATE_INDEX)
    for (uint32_t seq = 0; seq <= getLastSequence(); ++seq) {
        auto pool = loadBlock(seq);
        createTransactionsIndex(pool);
    }

    cslog() << "Recreated the index 0->" << getLastSequence() << ". Finishing with error now. Because we can";
    return false;
#else
    good_ = true;
    return true;
#endif
}

bool BlockChain::isGood() const {
    return good_;
}

uint64_t BlockChain::uuid() const {
    cs::Lock lock(dbLock_);
    return uuid_;
}

void BlockChain::onReadFromDB(csdb::Pool block, bool* shouldStop) {
    if (block.sequence() == 1) {
        cs::Lock lock(dbLock_);
        uuid_ = uuidFromBlock(block);
        csdebug() << "Blockchain: UUID = " << uuid_;
    }
    if (!blockValidator_->validateBlock(block, BlockValidator::ValidationLevel::hashIntergrity, BlockValidator::SeverityLevel::greaterThanWarnings)) {
        *shouldStop = true;
        return;
    }
    if (!updateWalletIds(block, *walletsCacheUpdater_.get())) {
        cserror() << "Blockchain: updateWalletIds() failed on block #" << block.sequence();
        *shouldStop = true;
    }
    else {
        walletsCacheUpdater_->loadNextBlock(block, block.confidants(), *this);
        if (!blockHashes_->initFromPrevBlock(block)) {
            cserror() << "Blockchain: blockHashes_->initFromPrevBlock(block) failed on block #" << block.sequence();
            *shouldStop = true;
        }
        else {
#ifdef TRANSACTIONS_INDEX
            const auto cnt_tr = block.transactions_count();
            if (cnt_tr > 0) {
                total_transactions_count_ += cnt_tr;

                if (lastNonEmptyBlock_.transCount && block.hash() != lastNonEmptyBlock_.hash) {
                    previousNonEmpty_[block.hash()] = lastNonEmptyBlock_;
                }
                lastNonEmptyBlock_.hash = block.hash();
                lastNonEmptyBlock_.transCount = static_cast<uint32_t>(block.transactions().size());
            }
#endif
        }
    }
}

bool BlockChain::postInitFromDB() {
    auto func = [](const WalletData::Address&, const WalletData& wallet) {
        double bal = wallet.balance_.to_double();
        if (bal < -std::numeric_limits<double>::min()) {
            csdebug() << "Wallet with negative balance (" << bal << ") detected: " << cs::Utils::byteStreamToHex(wallet.address_.data(), wallet.address_.size()) << " ("
                      << EncodeBase58(wallet.address_.data(), wallet.address_.data() + wallet.address_.size()) << ")";
        }
        return true;
    };
    walletsCacheStorage_->iterateOverWallets(func);
    return true;
}

#ifdef TRANSACTIONS_INDEX
void BlockChain::createTransactionsIndex(csdb::Pool& pool) {
#ifdef RECREATE_INDEX
    static std::map<csdb::Address, csdb::PoolHash> lapoos;
#endif

    // Update
    std::set<csdb::Address> indexedAddrs;

    auto lbd = [&indexedAddrs, &pool, this](const csdb::Address& addr) {
        auto key = getAddressByType(addr, BlockChain::AddressType::PublicKey);
        if (indexedAddrs.insert(key).second) {
#ifdef RECREATE_INDEX
            csdb::PoolHash lapoo = lapoos[key];
            lapoos[key] = pool.hash();
#else
            csdb::PoolHash lapoo = getLastTransaction(key).pool_hash();
#endif
            std::lock_guard<decltype(dbLock_)> l(dbLock_);
            storage_.set_previous_transaction_block(key, pool.hash(), lapoo);
        }
    };

    for (auto& tr : pool.transactions()) {
        lbd(tr.source());
        lbd(tr.target());
    }

    if (pool.transactions().size()) {
        total_transactions_count_ += pool.transactions().size();

        if (lastNonEmptyBlock_.transCount && pool.hash() != lastNonEmptyBlock_.hash) {
            previousNonEmpty_[pool.hash()] = lastNonEmptyBlock_;
        }

        lastNonEmptyBlock_.hash = pool.hash();
        lastNonEmptyBlock_.transCount = static_cast<uint32_t>(pool.transactions().size());
    }
}
#endif

cs::Sequence BlockChain::getLastSequence() const {
    std::lock_guard lock(dbLock_);

    if (deferredBlock_.is_valid()) {
        return deferredBlock_.sequence();
    }
    else if (!blockHashes_->empty()) {
        return blockHashes_->getDbStructure().last_;
    }
    else {
        return 0;
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

void BlockChain::iterateOverWallets(const std::function<bool(const cs::WalletsCache::WalletData::Address&, const cs::WalletsCache::WalletData&)> func) {
    std::lock_guard lock(cacheMutex_);
    walletsCacheStorage_->iterateOverWallets(func);
}

#ifdef MONITOR_NODE
void BlockChain::iterateOverWriters(const std::function<bool(const cs::WalletsCache::WalletData::Address&, const cs::WalletsCache::TrustedData&)> func) {
    std::lock_guard lock(cacheMutex_);
    walletsCacheStorage_->iterateOverWriters(func);
}

void BlockChain::applyToWallet(const csdb::Address& addr, const std::function<void(const cs::WalletsCache::WalletData&)> func) {
    std::lock_guard lock(cacheMutex_);
    WalletId id;
    if (!walletIds_->normal().find(addr, id)) {
        return;
    }
    auto wd = walletsCacheUpdater_->findWallet(id);

    func(*wd);
}
#endif

csdb::PoolHash BlockChain::getLastHash() const {
    std::lock_guard lock(dbLock_);

    if (deferredBlock_.is_valid()) {
        return deferredBlock_.hash().clone();
    }

    return storage_.last_hash();
}

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
    // storage loads blocks by 1-based index: 1 => pool[0], 2 => pool[1] etc.
    if (sequence > getLastSequence()) {
        return csdb::Pool{};
    }
    return storage_.pool_load(sequence + 1);
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

    if (deferredBlock_.hash() == transId.pool_hash()) {
        transaction = deferredBlock_.transaction(transId).clone();
        transaction.set_time(deferredBlock_.get_time());
    }
    else {
        transaction = storage_.transaction(transId);
        transaction.set_time(storage_.pool_load(transId.pool_hash()).get_time());
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

    const auto lastHash = blockHashes_->getLast();
    const csdb::PoolHash poolHash = pool.hash();

    if (lastHash == poolHash) {
        blockHashes_->removeLast();
        csmeta(csdebug) << "Remove last hash is ok, sequence: " << pool.sequence();
    }
    else {
        csmeta(cserror) << "Error! Last pool hash mismatch";
        const auto findSequence = blockHashes_->find(poolHash);
        const auto& bh = blockHashes_->getHashes();
        csmeta(cserror) << "Block hashes size: " << bh.size() << ", Pool sequence: " << pool.sequence() << ", in Block hashes sequence: " << findSequence
                        << (findSequence != 0 ? "" : " (hash not found)");
        // if (findSequence == 0) {
        //  for (std::size_t i = 0; i < bh.size(); ++i) {
        //    csmeta(csdebug) << "Block hash [" << i << "]: " << bh[i].to_string();
        //  }
        //}
    }

#ifdef TRANSACTIONS_INDEX
    total_transactions_count_ -= pool.transactions().size();
#endif

    removeWalletsInPoolFromCache(pool);

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
            cswarning() << "Some of Pool Signatures aren't valid. The pool will not be written to DB";
            return false;
        }
    }
    else {
        csmeta(csdebug) << "Genesis block will be written without signatures verification";
    }
    // pool signatures check: end

#ifdef TRANSACTIONS_INDEX
    createTransactionsIndex(pool);
#endif

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

    return blockHashes_->find(seq);
}

uint64_t BlockChain::getWalletsCountWithBalance() {
    std::lock_guard lock(cacheMutex_);

    uint64_t count = 0;
    auto proc = [&](const WalletData::Address& addr, const WalletData& wallet) {
        constexpr csdb::Amount zero_balance(0);
        if (!addr.empty() && wallet.balance_ >= zero_balance) {
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
    for (auto trIt = TransactionsIterator(*this, address); trIt.isValid(); trIt.next()) {
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

        const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(id);

        if (!wallDataPtr) {
            return false;
        }

        WalletsCache::convert(wallDataPtr->address_, wallPubKey);
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

template <typename WalletCacheProcessor>
bool BlockChain::updateWalletIds(const csdb::Pool& pool, WalletCacheProcessor& proc) {
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
                return false;
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

bool BlockChain::insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, WalletsCache::Initer& initer) {
    WalletId idSpecial{};

    if (!walletIds_->special().insertNormal(newWallAddress, newWalletId, idSpecial)) {
        cserror() << "Cannot add new wallet";
        return false;
    }

    if (WalletsIds::Special::isSpecial(idSpecial)) {
        if (!initer.moveData(idSpecial, newWalletId)) {
            cserror() << "Cannot move special wallet id data to newWalletId: idSpecial=" << idSpecial << " newWalletId=" << newWalletId;
            return false;
        }
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
}

bool BlockChain::getTransaction(const csdb::Address& addr, const int64_t& innerId, csdb::Transaction& result) const {
    cs::Lock lock(dbLock_);
    return storage_.get_from_blockchain(addr, innerId, result);
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
        if (!blockHashes_->loadNextBlock(nextPool)) {
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
    const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(id);

    if (wallDataPtr) {
        wallData = *wallDataPtr;
        return true;
    }

    return false;
}

bool BlockChain::getModifiedWallets(Mask& dest) const {
    std::lock_guard lock(cacheMutex_);

    bool isNewModified = (walletsCacheUpdater_->getModified().size() != dest.size()) || walletsCacheUpdater_->getModified().any();

    dest.resize(walletsCacheUpdater_->getModified().size(), true);
    dest |= walletsCacheUpdater_->getModified();

    return isNewModified;
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
    const auto last_seq = getLastSequence();
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

bool BlockChain::storeBlock(csdb::Pool& pool, bool bySync) {
    csdebug() << csfunc() << ":";

    const auto lastSequence = getLastSequence();
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

    auto lastSeq = getLastSequence() + 1;
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
    const auto firstSequence = getLastSequence() + 1;
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
    WalletId id;

    if (addr.is_wallet_id()) {
        id = addr.wallet_id();
    }

    else if (!walletIds_->normal().find(addr, id)) {
        return 0;
    }

    const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(id);

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

#ifdef TRANSACTIONS_INDEX
csdb::TransactionID BlockChain::getLastTransaction(const csdb::Address& addr) {
    std::lock_guard lock(cacheMutex_);
    WalletId id;

    if (addr.is_wallet_id()) {
        id = addr.wallet_id();
    }
    else if (!walletIds_->normal().find(addr, id)) {
        return csdb::TransactionID();
    }

    const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(id);

    if (!wallDataPtr) {
        return csdb::TransactionID();
    }

    return wallDataPtr->lastTransaction_;
}

csdb::PoolHash BlockChain::getPreviousPoolHash(const csdb::Address& addr, const csdb::PoolHash& ph) {
    std::lock_guard lock(dbLock_);
    return storage_.get_previous_transaction_block(getAddressByType(addr, BlockChain::AddressType::PublicKey), ph);
}

std::pair<csdb::PoolHash, uint32_t> BlockChain::getLastNonEmptyBlock() {
    std::lock_guard lock(dbLock_);
    return std::make_pair(lastNonEmptyBlock_.hash, lastNonEmptyBlock_.transCount);
}

std::pair<csdb::PoolHash, uint32_t> BlockChain::getPreviousNonEmptyBlock(const csdb::PoolHash& ph) {
    std::lock_guard lock(dbLock_);
    const auto it = previousNonEmpty_.find(ph);

    if (it != previousNonEmpty_.end()) {
        return std::make_pair(it->second.hash, it->second.transCount);
    }

    return std::pair<csdb::PoolHash, uint32_t>();
}

TransactionsIterator::TransactionsIterator(BlockChain& bc, const csdb::Address& addr)
: bc_(bc)
, addr_(addr) {
    setFromTransId(bc_.getLastTransaction(addr));
}

void TransactionsIterator::setFromTransId(const csdb::TransactionID& lTrans) {
    if (lTrans.is_valid()) {
        lapoo_ = bc_.loadBlock(lTrans.pool_hash());
        it_ = lapoo_.transactions().rbegin() + (lapoo_.transactions().size() - lTrans.index() - 1);
    }
    else {
        lapoo_ = csdb::Pool{};
    }
}

bool TransactionsIterator::isValid() const {
    return lapoo_.is_valid();
}

void TransactionsIterator::next() {
    while (++it_ != lapoo_.transactions().rend()) {
        if (bc_.isEqual(it_->source(), addr_) || bc_.isEqual(it_->target(), addr_)) {
            break;
        }
    }

    // Oops, no more in this blockfTransactionsListGet
    if (it_ == lapoo_.transactions().rend()) {
        auto ph = bc_.getPreviousPoolHash(addr_, lapoo_.hash());
        lapoo_ = bc_.loadBlock(ph);

        if (lapoo_.is_valid()) {
            it_ = lapoo_.transactions().rbegin();
            // transactions() cannot be empty
            if (!bc_.isEqual(it_->source(), addr_) && !bc_.isEqual(it_->target(), addr_)) {
                next();  // next should be executed only once
            }
        }
    }
}

#else

void TransactionsIterator::setFromHash(const csdb::PoolHash& ph) {
    auto hash = ph;
    bool found = false;

    while (!found) {
        lapoo_ = bc_.loadBlock(hash);

        if (!lapoo_.is_valid()) {
            break;
        }

        for (it_ = lapoo_.transactions().rbegin(); it_ != lapoo_.transactions().rend(); ++it_) {
            if (bc_.isEqual(it_->source(), addr_) || bc_.isEqual(it_->target(), addr_)) {
                found = true;
                break;
            }
        }

        hash = lapoo_.previous_hash();
    }
}

TransactionsIterator::TransactionsIterator(BlockChain& bc, const csdb::Address& addr)
: bc_(bc)
, addr_(addr) {
    setFromHash(bc_.getLastHash());
}

bool TransactionsIterator::isValid() const {
    return lapoo_.is_valid();
}

void TransactionsIterator::next() {
    bool found = false;

    while (++it_ != lapoo_.transactions().rend()) {
        if (bc_.isEqual(it_->source(), addr_) || bc_.isEqual(it_->target(), addr_)) {
            found = true;
            break;
        }
    }

    if (!found) {
        setFromHash(lapoo_.previous_hash());
    }
}

#endif
