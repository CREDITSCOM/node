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
    ~BlockHashes() = default;

    const DbStructure& getDbStructure() const {
        return db_;
    }

    bool empty() const {
        return size() == 0;
    }

    size_t size() const {
        return db_.last_;
    }

    void close();
    bool onNextBlock(const csdb::Pool& block);

    csdb::PoolHash find(cs::Sequence seq) const;
    cs::Sequence find(const csdb::PoolHash& hash) const;

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
};
}  // namespace cs

#endif  //  BLOCKHASHES_HPP
