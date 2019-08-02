#ifndef BLOCKHASHES_HPP
#define BLOCKHASHES_HPP

#include <csdb/pool.hpp>
#include <lmdb.hpp>

namespace cs {
class BlockHashes {
public:
    struct DbStructure {
        cs::Sequence first_;
        cs::Sequence last_;
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
        return seqDb_.size();
    }

    bool initFromPrevBlock(csdb::Pool prevBlock);
    bool loadNextBlock(csdb::Pool nextBlock);

    csdb::PoolHash find(cs::Sequence seq) const;
    cs::Sequence find(csdb::PoolHash hash) const;

    csdb::PoolHash removeLast();
    csdb::PoolHash getLast() const;

private slots:
    void onDbFailed(const cs::LmdbException& exception);

private:
    void initialization();

    DbStructure db_;
    bool isDbInited_;

    cs::Lmdb seqDb_;
    cs::Lmdb hashDb_;
};
}  // namespace cs

#endif  //  BLOCKHASHES_HPP
