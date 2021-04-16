#include <poolcache.hpp>

#include <csdb/pool.hpp>
#include <lib/system/utils.hpp>

static const std::string dbPath = "/poolcachedb";

cs::PoolCache::PoolCache(const std::string& path)
: db_(path + dbPath) {
    initialization();
}

cs::PoolCache::~PoolCache() {
    db_.close();
}

void cs::PoolCache::insert(const csdb::Pool& pool, PoolStoreType type) {
    cs::Bytes pBin = (type == PoolStoreType::Created ? pool.to_binary_updated() : pool.to_binary());
    insert(pool.sequence(), pBin, type);
    //if (type == PoolStoreType::Created)
    //{
    //    csdebug() << "created pool (" << pool.sequence() << "): " << cs::Utils::byteStreamToHex(pBin);
    //}

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
    return sequences_.find(sequence) != sequences_.end();
}

bool cs::PoolCache::isEmpty() const {
    return sequences_.empty();
}

cs::Sequence cs::PoolCache::minSequence() const {
    return (sequences_.begin())->first;
}

cs::Sequence cs::PoolCache::maxSequence() const {
    return std::prev(sequences_.end())->first;
}

std::optional<cs::PoolCache::Data> cs::PoolCache::value(cs::Sequence sequence) const {
    auto bytes = db_.value<cs::Bytes>(sequence);
    Data data { csdb::Pool::from_binary(std::move(bytes)), cachedType(sequence) };

    if (data.pool.sequence() != sequence) {
        csdebug() << __func__ << ": data size = " << bytes.size() << ", content = " << cs::Utils::byteStreamToHex(bytes);
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
    return sequences_.size();
}

size_t cs::PoolCache::sizeSynced() const {
    return syncedPoolSize_;
}

size_t cs::PoolCache::sizeCreated() const {
    return size() - sizeSynced();
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

    if (sizeSynced() > 0) {
        auto syncedInterval = createInterval(minSequence(), syncedIter->first);

        if (!syncedInterval.empty()) {
            intervals.insert(intervals.end(), std::begin(syncedInterval), std::end(syncedInterval));
        }
    }

    if (sizeCreated() > 0) {
        auto i = std::as_const(syncedIter);
        auto iter = (syncedIter == sequences_.end()) ? sequences_.begin() : i;
        auto createdInterval = createInterval(std::next(iter)->first, maxSequence());

        if (sizeSynced() > 0 && !createdInterval.empty()) {
            if (syncedIter->first != std::next(syncedIter)->first) {
                intervals.push_back(std::make_pair(syncedIter->first + 1, std::next(syncedIter)->first - 1));
            }
        }

        if (!createdInterval.empty()) {
            intervals.insert(intervals.end(), std::begin(createdInterval), std::end(createdInterval));
        }
    }

    return intervals;
}

void cs::PoolCache::onInserted(const char* data, size_t size) {
    auto [iter, ok] = sequences_.emplace(cs::Lmdb::convert<cs::Sequence>(data, size), type_);

    if (!ok) {
        return;
    }

    if (type_ == cs::PoolStoreType::Synced) {
        ++syncedPoolSize_;

        if (syncedIter == sequences_.end()) {
            syncedIter = iter;
        }

        if (iter->first > syncedIter->first) {
            syncedIter = iter;
        }
    }
}

void cs::PoolCache::onRemoved(const char* data, size_t size) {
    onRemoved(cs::Lmdb::convert<cs::Sequence>(data, size));
}

void cs::PoolCache::onRemoved(cs::Sequence sequence) {
    const auto iter = sequences_.find(sequence);

    if (iter != sequences_.end()) {
        if (iter->second == cs::PoolStoreType::Synced) {
            --syncedPoolSize_;
        }

        if (iter == syncedIter) {
            syncedIter = syncedPoolSize_ > 0 ? std::prev(syncedIter) : sequences_.end();
        }

        sequences_.erase(iter);
    }
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

    syncedIter = sequences_.end();
}

cs::PoolStoreType cs::PoolCache::cachedType(cs::Sequence sequence) const {
    return sequences_.find(sequence)->second;
}

std::vector<cs::PoolCache::Interval> cs::PoolCache::createInterval(Sequence min, Sequence max) const {
    std::vector<cs::PoolCache::Interval> intervals;

    auto isFreeSpace = false;
    auto adder = [&](cs::Sequence begin, cs::Sequence end) {
        intervals.push_back(std::make_pair(begin, end));
    };

    for (auto value = min; value <= max; ++value) {
        if (contains(value)) {
            if (isFreeSpace) {
                adder(min, value - 1);
                isFreeSpace = false;
            }
        }
        else {
            if (!isFreeSpace) {
                min = value;
                isFreeSpace = true;
            }
        }
    }

    return intervals;
}
