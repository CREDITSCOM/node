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

bool BlockHashes::on_next_block(const csdb::Pool& block, bool fast_mode) {
    cs::Sequence seq = block.sequence();
    if (db_.last_ > 0) {
        if (seq <= db_.last_) {
            // already cached
            return false;
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

    if (!flush_completed) {
        // accumulate in mem_cache
        (*active_mem_cache)[seq] = hash;
    }
    else {
        if (active_mem_cache->empty() && !fast_mode) {
            // add item directly
            seqDb_.insert(seq, hash.to_binary());
            hashDb_.insert(hash.to_binary(), seq);
        }
        else {
            (*active_mem_cache)[seq] = hash;
            // swap mem_caches and start store to DB in separate thread
            flush_mem_cache();
        }
    }

    return true;
}

bool BlockHashes::onReadBlock(const csdb::Pool& block) {
    return on_next_block(block, true); // fast mode
}

bool BlockHashes::onStoreBlock(const csdb::Pool& block) {
    cs::RoundNumber cur_round = Conveyer::instance().currentRoundNumber();
    cs::Sequence seq = block.sequence();
    if (cur_round > seq && cur_round - seq > 1) {
        return on_next_block(block, true); // fast mode, sync goes on
    }
    return on_next_block(block, false); // normal mode
}

csdb::PoolHash BlockHashes::find(cs::Sequence seq) const {
    if (seq > db_.last_) {
        return csdb::PoolHash{};
    }
    if (!active_mem_cache->empty()) {
        if (active_mem_cache->count(seq) > 0) {
            return (*active_mem_cache)[seq];
        }
    }
    if (seqDb_.size() == 0 || !seqDb_.isKeyExists(seq)) {
        // if !flush_completed there may be an item still on way to DB
        return csdb::PoolHash{};
    }
    auto value = seqDb_.value<cs::Bytes>(seq);
    return csdb::PoolHash::from_binary(std::move(value));
}

cs::Sequence BlockHashes::find(csdb::PoolHash hash) const {
    // search may be too long:
    //if (!active_mem_cache->empty()) {
    //    for (const auto& item : *active_mem_cache) {
    //        if (item.second == hash) {
    //            return item.first;
    //        }
    //    }
    //}
    if (seqDb_.size() == 0 || !hashDb_.isKeyExists(hash.to_binary())) {
        // if !flush_completed there may be an item still on way to DB
        return cs::kWrongSequence;
    }

    return hashDb_.value<cs::Sequence>(hash.to_binary());
}

bool BlockHashes::wait_flush_completed(uint32_t sleep_msec, uint32_t count) {
    if (!flush_completed) {
        uint32_t cnt = 0;
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_msec));
            if (++cnt >= count) {
                return false;;
            }
        } while (!flush_completed);
    }
    return true;
}

csdb::PoolHash BlockHashes::removeLast() {
    if (!active_mem_cache->empty()) {
        csdb::PoolHash tmp = active_mem_cache->crbegin()->second.clone();
        active_mem_cache->erase(active_mem_cache->crbegin()->first);
        --db_.last_;
        return tmp;
    }
    if (empty()) {
        return csdb::PoolHash{};
    }
    if (!flush_completed) {
        if (!wait_flush_completed(20, 50)) {
            // wait for flushing to complete failed
            return csdb::PoolHash{};
        }
    }
    auto pair = seqDb_.last<cs::Sequence, cs::Bytes>();
    seqDb_.remove(pair.first);
    hashDb_.remove(pair.second);

    if (db_.last_ == pair.first) {
        --db_.last_;
    }
    else {
        db_.last_ = pair.first - 1;
    }

    return csdb::PoolHash::from_binary(std::move(pair.second));
}

csdb::PoolHash BlockHashes::getLast() {
    if (!active_mem_cache->empty()) {
        return active_mem_cache->crbegin()->second;
    }
    if (!wait_flush_completed(20, 25)) {
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
    active_mem_cache = &mem_cache1;
    flush_completed = true;

    cs::Connector::connect(&seqDb_.failed, this, &BlockHashes::onDbFailed);
    cs::Connector::connect(&hashDb_.failed, this, &BlockHashes::onDbFailed);

    seqDb_.setMapSize(cs::Lmdb::Default1GbMapSize);
    hashDb_.setMapSize(cs::Lmdb::Default1GbMapSize);

    seqDb_.open();
    hashDb_.open();
}

void BlockHashes::flush_mem_cache() {
    if (!flush_completed) {
        return;
    }
    if (active_mem_cache->empty()) {
        // nothing to store
        return;
    }
    flush_completed = false;
    
    std::map<cs::Sequence, csdb::PoolHash>* flushing_cache = active_mem_cache;
    if (active_mem_cache == &mem_cache1) {
        active_mem_cache = &mem_cache2;
    }
    else if (active_mem_cache == &mem_cache2) {
        active_mem_cache = &mem_cache1;
    }
    else {
        // incorrect active_mem_cache
        return;
    }
    active_mem_cache->clear();

    std::thread t(
        [this, flushing_cache]() {
            size_t cnt = 0;
            for (const auto& item : *flushing_cache) {
                seqDb_.insert(item.first, item.second.to_binary());
                hashDb_.insert(item.second.to_binary(), item.first);
                ++cnt;
            }
            flush_completed = true;
        }
    );
    t.detach();
}

}  // namespace cs
