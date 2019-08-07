#include <csnode/blockhashes.hpp>

#include <conveyer.hpp>
#include <cstring>

#include <lib/system/logger.hpp>

static const char* seqPath = "/seqdb";
static const char* hashPath = "/hashdb";

namespace cs {
BlockHashes::BlockHashes(const std::string& path)
: db_{}
, isDbInited_(false)
, seqDb_(path + seqPath)
, hashDb_(path + hashPath) {
    initialization();
}

void BlockHashes::close() {
    if (seqDb_.isOpen()) {
        seqDb_.close();
    }

    if (hashDb_.isOpen()) {
        hashDb_.close();
    }

    while (!isFlushCompleted_);
}

bool BlockHashes::onNextBlock(const csdb::Pool& block, bool fastMode) {
    cs::Sequence seq = block.sequence();

    if (db_.last_ > 0) {
        if (seq <= db_.last_) {
            return false; // already cached
        }
    }

    db_.last_ = seq;

    if (!isDbInited_) {
        db_.first_ = 0;
        isDbInited_ = true;
    }

    auto hash = block.hash();
    auto cached_hash = find(seq);

    if (cached_hash == hash) {
        return true;
    }

    if (!isFlushCompleted_) {
        (*activeMemoryCache_)[seq] = hash; // accumulate in mem_cache
    }
    else {
        if (activeMemoryCache_->empty() && !fastMode) {
            // add item directly
            seqDb_.insert(seq, hash.to_binary());
            hashDb_.insert(hash.to_binary(), seq);
        }
        else {
            (*activeMemoryCache_)[seq] = hash;
            flushMemoryCache(); // swap mem_caches and start store to DB in separate thread
        }
    }

    return true;
}

bool BlockHashes::onReadBlock(const csdb::Pool& block) {
    return onNextBlock(block, true); // fast mode
}

bool BlockHashes::onStoreBlock(const csdb::Pool& block) {
    auto currentRound = Conveyer::instance().currentRoundNumber();
    auto seq = block.sequence();

    if (currentRound > seq && currentRound - seq > 1) {
        return onNextBlock(block, true); // fast mode, sync goes on
    }

    return onNextBlock(block, false); // normal mode
}

csdb::PoolHash BlockHashes::find(cs::Sequence seq) const {
    if (seq > db_.last_) {
        return csdb::PoolHash{};
    }

    if (!activeMemoryCache_->empty()) {
        if (activeMemoryCache_->count(seq) > 0) {
            return (*activeMemoryCache_)[seq];
        }
    }

    // if !flush_completed there may be an item still on way to DB
    if (seqDb_.size() == 0 || !seqDb_.isKeyExists(seq)) {
        return csdb::PoolHash{};
    }

    auto value = seqDb_.value<cs::Bytes>(seq);
    return csdb::PoolHash::from_binary(std::move(value));
}

cs::Sequence BlockHashes::find(const csdb::PoolHash& hash) const {
    // if !flush_completed there may be an item still on way to DB
    if (seqDb_.size() == 0 || !hashDb_.isKeyExists(hash.to_binary())) {
        return cs::kWrongSequence;
    }

    return hashDb_.value<cs::Sequence>(hash.to_binary());
}

bool BlockHashes::waitFlushCompleted(uint32_t sleepMsec, uint32_t count) {
    if (!isFlushCompleted_) {
        uint32_t cnt = 0;

        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMsec));
            if (++cnt >= count) {
                return false;;
            }
        } while (!isFlushCompleted_);
    }

    return true;
}

csdb::PoolHash BlockHashes::removeLast() {
    if (!activeMemoryCache_->empty()) {
        csdb::PoolHash tmp = activeMemoryCache_->crbegin()->second.clone();
        activeMemoryCache_->erase(activeMemoryCache_->crbegin()->first);
        --db_.last_;
        return tmp;
    }

    if (empty()) {
        return csdb::PoolHash{};
    }

    if (!isFlushCompleted_) {
        if (!waitFlushCompleted(20, 50)) {
            return csdb::PoolHash{}; // wait for flushing to complete failed
        }
    }

    auto [sequence, hash] = seqDb_.last<cs::Sequence, cs::Bytes>();
    seqDb_.remove(sequence);
    hashDb_.remove(hash);

    if (db_.last_ == sequence) {
        --db_.last_;
    }
    else {
        db_.last_ = sequence - 1;
    }

    return csdb::PoolHash::from_binary(std::move(hash));
}

csdb::PoolHash BlockHashes::getLast() {
    if (!activeMemoryCache_->empty()) {
        return activeMemoryCache_->crbegin()->second;
    }

    if (!waitFlushCompleted(20, 25)) {
        return csdb::PoolHash{};
    }

    if (seqDb_.size() == 0) {
        return csdb::PoolHash{};
    }

    auto pair = seqDb_.last<cs::Sequence, cs::Bytes>();
    return csdb::PoolHash::from_binary(std::move(pair.second));
}

void BlockHashes::onDbFailed(const LmdbException& exception) {
    cswarning() << csfunc() << ", block hashes database exception: " << exception.what();
}

void BlockHashes::initialization() {
    activeMemoryCache_ = &memoryCache1_;
    isFlushCompleted_ = true;

    cs::Connector::connect(&seqDb_.failed, this, &BlockHashes::onDbFailed);
    cs::Connector::connect(&hashDb_.failed, this, &BlockHashes::onDbFailed);

    seqDb_.setMapSize(cs::Lmdb::Default1GbMapSize);
    hashDb_.setMapSize(cs::Lmdb::Default1GbMapSize);

    seqDb_.open();
    hashDb_.open();
}

void BlockHashes::flushMemoryCache() {
    if (!isFlushCompleted_) {
        return;
    }

    if (activeMemoryCache_->empty()) {
        return; // nothing to store
    }

    isFlushCompleted_ = false;
    
    if (activeMemoryCache_ == &memoryCache1_) {
        activeMemoryCache_ = &memoryCache2_;
    }
    else if (activeMemoryCache_ == &memoryCache2_) {
        activeMemoryCache_ = &memoryCache1_;
    }
    else {
        return; // incorrect active_mem_cache
    }

    activeMemoryCache_->clear();

    std::thread t([this, flushingCache = activeMemoryCache_]() {
            size_t count = 0;

            for (const auto& [sequence, hash] : *flushingCache) {
                const auto hashBinary = hash.to_binary();

                seqDb_.insert(sequence, hashBinary);
                hashDb_.insert(hashBinary, sequence);

                ++count;
            }

            isFlushCompleted_ = true;
        }
    );

    t.detach();
}

}  // namespace cs
