#include <transactionsindex.hpp>

#include <algorithm>
#include <set>
#include <vector>

#include <boost/filesystem.hpp>

#include <csdb/internal/utils.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <lib/system/logger.hpp>
#include <csnode/blockchain.hpp>
#include <csnode/transactionsiterator.hpp>

namespace {
constexpr const char* kDbPath = "/indexdb";
constexpr const char* kLastIndexedPath =  "/last_indexed";

auto getTrxIndexKey(const cs::PublicKey& _pubKey, cs::Sequence _seq) {
    cs::Bytes ret(_pubKey.begin(), _pubKey.end());
    ret.resize(ret.size() + sizeof(_seq));

    auto ptr = reinterpret_cast<uint8_t*>(&_seq);
    std::copy(ptr, ptr + sizeof(_seq), ret.begin() + _pubKey.size());
    return ret;
}
} // namespace

namespace cs {

TransactionsIndex::TransactionsIndex(BlockChain& _bc, const std::string& _path, bool _recreate)
    : bc_(_bc),
      rootPath_(_path),
      db_(std::make_unique<Lmdb>(_path + kDbPath)),
      recreate_(_recreate ? true : hasToRecreate(_path + kLastIndexedPath, lastIndexedPool_)),
      lastIndexedFile_(_path + kLastIndexedPath, sizeof(cs::Sequence)) {
    init();
}

void TransactionsIndex::onStartReadFromDb(Sequence _lastWrittenPoolSeq) {
    if (!recreate_ && lastIndexedPool_ != _lastWrittenPoolSeq) {
        recreate_ = true;
    }
}

void TransactionsIndex::onReadFromDb(const csdb::Pool& _pool) {
    if (_pool.sequence() == 0 && recreate_) {
        reset();
        init();
    }

    if (recreate_ || lastIndexedPool_ < _pool.sequence()) {
        updateFromNextBlock(_pool);
    }
}

void TransactionsIndex::onDbReadFinished() {
    if (recreate_) {
        recreate_ = false;
        lapoos_.clear();
        cslog() << "Recreated index 0 -> " << lastIndexedPool_
                << ". Continue to keep it actual from new blocks.";
    }
    else {
        updateLastIndexed();
    }
}

void TransactionsIndex::onRemoveBlock(const csdb::Pool& _pool) {
    std::set<csdb::Address> uniqueAddresses;
    std::vector<std::pair<cs::PublicKey, csdb::TransactionID>> updates;

    auto lbd = [&_pool, &updates, &uniqueAddresses, this](const csdb::Address& _addr, cs::Sequence _sq) {
        auto key = bc_.getAddressByType(_addr, BlockChain::AddressType::PublicKey);

        if (uniqueAddresses.insert(key).second) {
            auto it = TransactionsIterator(bc_, _addr, _pool);
            bool found = false;

            for (; it.isValid(); it.next()) {
                if (it->id().pool_seq() < _sq) {
                    updates.push_back(std::make_pair(key.public_key(), it->id()));
                    found = true;
                    break;
                }
            }
            if (!found) {
                updates.push_back(std::make_pair(key.public_key(),
                                                 csdb::TransactionID(kWrongSequence, kWrongSequence)));
            }
            removeLastTransBlock(key.public_key(), _sq);
        }
    };

    for (const auto& t : _pool.transactions()) {
        lbd(t.source(), lastIndexedPool_);
        lbd(t.target(), lastIndexedPool_);
    }
    --lastIndexedPool_;
    updateLastIndexed();

    if (updates.size()) {
        bc_.updateLastTransactions(updates);
    }
}

void TransactionsIndex::update(const csdb::Pool& _pool) {
    updateFromNextBlock(_pool);
}

void TransactionsIndex::invalidate() {
    lastIndexedPool_ = kWrongSequence;
    updateLastIndexed();
}

void TransactionsIndex::close() {
    if (db_->isOpen()) {
      db_->close();
    }
}

void TransactionsIndex::updateFromNextBlock(const csdb::Pool& _pool) {
    std::set<csdb::Address> indexedAddrs;

    auto lbd = [&indexedAddrs, &_pool, this](const csdb::Address& _addr) {
        auto key = bc_.getAddressByType(_addr, BlockChain::AddressType::PublicKey);

        if (indexedAddrs.insert(key).second) {
            Sequence lapoo;
            if (recreate_) {
                auto it = lapoos_.find(key);
                if (it == lapoos_.end()) {
                    lapoo = cs::kWrongSequence;
                }
                else {
                    lapoo = it->second;
                }
                lapoos_[key] = _pool.sequence();
            }
            else {
                lapoo = bc_.getLastTransaction(key).pool_seq();
            }

            if (lapoo != _pool.sequence()) {
                setPrevTransBlock(key.public_key(), _pool.sequence(), lapoo);
            }
            else {
                cserror() << "Attempt to make trx index inconsistent, curr pool num is "
                          << _pool.sequence() << ", prev pool num is " << lapoo
                          << ". For public key: "
                          << EncodeBase58(key.public_key().data(), key.public_key().data() + key.public_key().size())
                          << ", recreate status is " << recreate_;
            }
        }
    };

    for (auto& tr : _pool.transactions()) {
        lbd(tr.source());
        lbd(tr.target());
    }

    lastIndexedPool_ = _pool.sequence();
    updateLastIndexed();
}

void TransactionsIndex::setPrevTransBlock(const PublicKey& _pubKey, cs::Sequence _curr, cs::Sequence _prev) {
    db_->insert(getTrxIndexKey(_pubKey, _curr), _prev);
}

void TransactionsIndex::removeLastTransBlock(const PublicKey& _pubKey, cs::Sequence _curr) {
    db_->remove(getTrxIndexKey(_pubKey, _curr));
}

Sequence TransactionsIndex::getPrevTransBlock(const csdb::Address& _addr, Sequence _prev) const {
    auto key = getTrxIndexKey(bc_.getAddressByType(_addr, BlockChain::AddressType::PublicKey).public_key(), _prev);
    if (db_->size() == 0 || !db_->isKeyExists(key)) {
        return kWrongSequence;
    }
    return db_->value<Sequence>(key);
}

inline bool TransactionsIndex::hasToRecreate(const std::string& _lastIndFilePath,
                                             cs::Sequence& _lastIndexedPool) {
    boost::filesystem::path p(_lastIndFilePath);
    if (!boost::filesystem::is_regular_file(p)) {
        return true;
    }
    MMappedFileWrap<FileSource> f(_lastIndFilePath, sizeof(cs::Sequence), false);
    if (!f.isOpen()) {
        return true;
    }
    _lastIndexedPool = *(f.data<const cs::Sequence>());
    if (_lastIndexedPool == kWrongSequence) {
        return true;
    }
    return false;
}

inline void TransactionsIndex::updateLastIndexed() {
    auto ptr = lastIndexedFile_.data<cs::Sequence>();
    if (ptr) {
        *ptr = lastIndexedPool_;
    }
}

void TransactionsIndex::onDbFailed(const LmdbException& e) {
    cswarning() << csfunc() << ", transactions index database exception " << e.what();
}

inline void TransactionsIndex::init() {
    Connector::connect(&db_->failed, this, &TransactionsIndex::onDbFailed);

    db_->setMapSize(cs::Lmdb::Default1GbMapSize);
    db_->open();
}

inline void TransactionsIndex::reset() {
    Connector::disconnect(&db_->failed, this, &TransactionsIndex::onDbFailed);
    db_->close();
    db_.reset(nullptr);
    csdb::internal::path_remove(rootPath_ + kDbPath);
    db_ = std::make_unique<Lmdb>(rootPath_ + kDbPath);
}
} // namespace cs
