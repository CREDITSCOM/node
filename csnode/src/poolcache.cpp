#include <poolcache.hpp>

#include <csdb/pool.hpp>

static const std::string dbPath = "/poolcachedb";

cs::PoolCache::PoolCache(const std::string& path)
: db_(path + dbPath) {
    initialization();
}

cs::PoolCache::~PoolCache() {
    db_.close();
}

void cs::PoolCache::insert(const csdb::Pool& pool, PoolStoreType type) {
    insert(pool.sequence(), pool.to_binary(), type);
}

void cs::PoolCache::insert(cs::Sequence sequence, const cs::Bytes& bytes, cs::PoolStoreType type) {
    type_ = type;
    db_.insert(sequence, bytes);
}

bool cs::PoolCache::remove(cs::Sequence sequence) {
    return db_.remove(sequence);
}

void cs::PoolCache::remove(cs::Sequence from, cs::Sequence to) {
    if (from > to) {
        return;
    }

    for (; from <= to; ++from) {
        remove(from);
    }
}

bool cs::PoolCache::contains(cs::Sequence sequence) const {
    auto result = syncedSequences_.find(sequence) != syncedSequences_.end();

    if (result) {
        return result;
    }

    return createdSequences_.find(sequence) != createdSequences_.end();
}

bool cs::PoolCache::isEmpty() const {
    return syncedSequences_.empty() && createdSequences_.empty();
}

bool cs::PoolCache::isSyncedEmpty() const {
    return syncedSequences_.empty();
}

bool cs::PoolCache::isCreatedEmpty() const {
    return createdSequences_.empty();
}

cs::Sequence cs::PoolCache::minSequence() const {
    cs::Sequence minSynced = 0;
    cs::Sequence minCreated = 0;

    if (!syncedSequences_.empty()) {
        minSynced = *(syncedSequences_.begin());
    }

    if (!createdSequences_.empty()) {
        minCreated = *(createdSequences_.begin());
    }

    if (minSynced == 0 && minCreated != 0) {
        return minCreated;
    }
    else if (minSynced !=0 && minCreated == 0) {
        return minSynced;
    }
    else {
        return std::min(minSynced, minCreated);
    }
}

cs::Sequence cs::PoolCache::maxSequence() const {
    cs::Sequence maxSynced = 0;
    cs::Sequence maxCreated = 0;

    if (!syncedSequences_.empty()) {
        maxSynced = *std::prev((syncedSequences_.end()));
    }

    if (!createdSequences_.empty()) {
        maxCreated = *std::prev((createdSequences_.end()));
    }

    if (maxSynced == 0 && maxCreated != 0) {
        return maxCreated;
    }
    else if (maxSynced !=0 && maxCreated == 0) {
        return maxSynced;
    }
    else {
        return std::max(maxSynced, maxCreated);
    }
}

cs::Sequence cs::PoolCache::maxSyncedSequence() const {
    return *std::prev(syncedSequences_.end());
}

std::optional<cs::PoolCache::Data> cs::PoolCache::value(cs::Sequence sequence) const {
    auto bytes = db_.value<cs::Bytes>(sequence);
    Data data { csdb::Pool::from_binary(std::move(bytes)), cachedType(sequence) };

    if (data.pool.sequence() != sequence) {
        return std::nullopt;
    }

    return std::make_optional(std::move(data));
}

std::optional<cs::PoolCache::Data> cs::PoolCache::pop(cs::Sequence sequence) {
    auto data = value(sequence);
    db_.remove(sequence);

    if (!data.has_value()) {
        onRemoved(sequence);
        return std::nullopt;
    }

    return std::make_optional(std::move(data).value());
}

size_t cs::PoolCache::size() const {
    return syncedSequences_.size() + createdSequences_.size();
}

size_t cs::PoolCache::sizeSynced() const {
    return syncedSequences_.size();
}

size_t cs::PoolCache::sizeCreated() const {
    return createdSequences_.size();
}

void cs::PoolCache::clear() {
    if (isEmpty()) {
        return;
    }

    auto min = minSequence();
    auto max = maxSequence();

    for (; min <= max; ++min) {
        remove(min);
    }
}

std::vector<cs::PoolCache::Interval> cs::PoolCache::ranges() const {
    std::vector<Interval> intervals;

    if (isEmpty()) {
        return intervals;
    }

    auto adder = [&](cs::Sequence begin, cs::Sequence end) {
        intervals.push_back(std::make_pair(begin, end));
    };

    auto start = minSequence();
    auto max = maxSyncedSequence();
    auto isFreeSpace = false;

    for (auto value = minSequence(); value <= max; ++value) {
        if (contains(value)) {
            if (isFreeSpace) {
                adder(start, value - 1);
                isFreeSpace = false;
            }
        }
        else {
            if (!isFreeSpace) {
                start = value;
                isFreeSpace = true;
            }
        }
    }

    return intervals;
}

void cs::PoolCache::onInserted(const char* data, size_t size) {
    const auto seq = cs::Lmdb::convert<cs::Sequence>(data, size);
    type_ == cs::PoolStoreType::Synced ? syncedSequences_.emplace(seq) : createdSequences_.emplace(seq);
}

void cs::PoolCache::onRemoved(const char* data, size_t size) {
    onRemoved(cs::Lmdb::convert<cs::Sequence>(data, size));
}

void cs::PoolCache::onRemoved(cs::Sequence sequence) {
    syncedSequences_.erase(sequence);
    createdSequences_.erase(sequence);
}

void cs::PoolCache::onFailed(const cs::LmdbException& exception) {
    cswarning() << csfunc() << ", pool caches database exception: " << exception.what();
}

void cs::PoolCache::initialization() {
    cs::Connector::connect(&db_.commited, this, &cs::PoolCache::onInserted);
    cs::Connector::connect(&db_.removed, this, static_cast<void(cs::PoolCache::*)(const char*, size_t)>(&cs::PoolCache::onRemoved));
    cs::Connector::connect(&db_.failed, this, &cs::PoolCache::onFailed);

    db_.setMapSize(cs::Lmdb::Default1GbMapSize);
    db_.open();
}

cs::PoolStoreType cs::PoolCache::cachedType(Sequence sequence) const {
    auto iter = syncedSequences_.find(sequence);

    if (iter != syncedSequences_.end()) {
        return cs::PoolStoreType::Synced;
    }

    return cs::PoolStoreType::Created;
}
