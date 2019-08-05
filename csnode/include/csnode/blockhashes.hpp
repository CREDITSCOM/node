#ifndef BLOCKHASHES_HPP
#define BLOCKHASHES_HPP

#include <map>

#include <csdb/pool.hpp>
#include <lmdb.hpp>

namespace cs {
class BlockHashes {
public:
    struct DbStructure {
        cs::Sequence first_{ 0 };
        cs::Sequence last_{ 0 };
    };

public:
    explicit BlockHashes(const std::string& path);

    const DbStructure& getDbStructure() const {
        return db_;
    }

    bool empty() const {
        return size() == 0;
    }

    size_t size() const {
        return db_.last_;
        //return seqDb_.size();
    }

    bool onReadBlock(const csdb::Pool& block);
    bool onStoreBlock(const csdb::Pool& block);

    csdb::PoolHash find(cs::Sequence seq) const;
    cs::Sequence find(csdb::PoolHash hash) const;

    csdb::PoolHash removeLast();
    csdb::PoolHash getLast();

private slots:
    void onDbFailed(const cs::LmdbException& exception);

private:
    void initialization();

    DbStructure db_;
    bool isDbInited_;

    cs::Lmdb seqDb_;
    cs::Lmdb hashDb_;

    std::map<cs::Sequence, csdb::PoolHash> mem_cache1;
    std::map<cs::Sequence, csdb::PoolHash> mem_cache2;
    std::map<cs::Sequence, csdb::PoolHash>* active_mem_cache;
    std::atomic<bool> flush_completed;

    void flush_mem_cache();
    bool on_next_block(const csdb::Pool& block, bool fast_mode);
    bool wait_flush_completed(uint32_t sleep_msec, uint32_t count);
};
}  // namespace cs

#endif  //  BLOCKHASHES_HPP
