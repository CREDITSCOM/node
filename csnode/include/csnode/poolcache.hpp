#ifndef POOLCACHE_HPP
#define POOLCACHE_HPP

#include <map>
#include <optional>

#include <lmdb.hpp>
#include <nodecore.hpp>

namespace csdb {
class Pool;
}

namespace cs {
// storage for temporary pools received by sync or applyCharacterictic
class PoolCache {
public:
    using Interval = std::pair<cs::Sequence, cs::Sequence>;

    struct Data {
        csdb::Pool pool;
        cs::PoolStoreType type;
    };

    explicit PoolCache(const std::string& path);
    ~PoolCache();

    // add new pool to db
    void insert(const csdb::Pool& pool, cs::PoolStoreType type);
    void insert(cs::Sequence sequence, const cs::Bytes& bytes, cs::PoolStoreType type);

    // removes pool from db
    bool remove(cs::Sequence sequence);

    // removes all range [from, to]
    void remove(cs::Sequence from, cs::Sequence to);

    // returns existance of sequence at db
    bool contains(cs::Sequence sequence) const;
    bool isEmpty() const;

    // check on isEmpty() before use it, or UB
    cs::Sequence minSequence() const;
    cs::Sequence maxSequence() const;

    // returns value by key
    std::optional<Data> value(cs::Sequence sequence) const;

    // returns pool and remove key sequence
    std::optional<Data> pop(cs::Sequence sequence);

    // returns all pool cache size
    size_t size() const;

    // returns synced pools size
    size_t sizeSynced() const;

    // returns created pools size
    size_t sizeCreated() const;

    void clear();

    // returns free spaces at pool caches ranges
    std::vector<Interval> ranges() const;

private slots:
    void onInserted(const char* data, size_t size);
    void onRemoved(const char* data, size_t size);
    void onRemoved(cs::Sequence sequence);
    void onFailed(const cs::LmdbException& exception);

private:
    void initialization();
    cs::PoolStoreType cachedType(cs::Sequence sequence) const;
    
    std::vector<Interval> createInterval(cs::Sequence min, cs::Sequence max) const;

    size_t syncedPoolSize_ = 0;
    cs::PoolStoreType type_;

    std::map<cs::Sequence, cs::PoolStoreType> sequences_;
    decltype(sequences_)::iterator syncedIter;

    cs::Lmdb db_;
};
}

#endif // POOLCACHE_HPP
