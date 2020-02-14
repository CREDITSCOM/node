#ifndef POOLCACHE_HPP
#define POOLCACHE_HPP

#include <set>
#include <optional>

#include <lmdb.hpp>

namespace csdb {
class Pool;
}

namespace cs {
// storage for temporary pools received by sync or applyCharacterictic
class PoolCache {
public:
    explicit PoolCache(const std::string& path);
    ~PoolCache();

    // add new pool to db
    void insert(const csdb::Pool& pool);
    void insert(cs::Sequence sequence, const cs::Bytes& bytes);

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

private slots:
    void onInserted(const char* data, size_t size);
    void onRemoved(const char* data, size_t size);
    void onFailed(const cs::LmdbException& exception);

private:
    void initialization();
    
    std::set<cs::Sequence> sequences_;
    cs::Lmdb db_;
};
}

#endif // POOLCACHE_HPP
