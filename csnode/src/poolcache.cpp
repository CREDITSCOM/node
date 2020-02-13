#include <poolcache.hpp>

#include <csdb/pool.hpp>

namespace {
const std::string dbPath = "/hashdb";
}

cs::PoolCache::PoolCache(const std::string& path)
: db_(path + dbPath) {
    initialization();
}

cs::PoolCache::~PoolCache() {
    db_.close();
}

void cs::PoolCache::insert(const csdb::Pool& pool) {
    insert(pool.sequence(), pool.to_binary());
}

void cs::PoolCache::insert(cs::Sequence sequence, const cs::Bytes& bytes) {
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
    return *(sequences_.begin());
}

cs::Sequence cs::PoolCache::maxSequence() const {
    return *(std::prev(sequences_.end()));
}

void cs::PoolCache::onInserted(const char* data, size_t size) {
    sequences_.insert(cs::Lmdb::convert<cs::Sequence>(data, size));
}

void cs::PoolCache::onRemoved(const char* data, size_t size) {
    sequences_.erase(cs::Lmdb::convert<cs::Sequence>(data, size));
}

void cs::PoolCache::onFailed(const cs::LmdbException& exception) {
    cswarning() << csfunc() << ", pool caches database exception: " << exception.what();
}

void cs::PoolCache::initialization() {
    cs::Connector::connect(&db_.commited, this, &cs::PoolCache::onInserted);
    cs::Connector::connect(&db_.removed, this, &cs::PoolCache::onRemoved);
    cs::Connector::connect(&db_.failed, this, &cs::PoolCache::onFailed);

    db_.setMapSize(cs::Lmdb::Default1GbMapSize);
    db_.open();
}
