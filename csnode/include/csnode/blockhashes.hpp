#ifndef BLOCKHASHES_HPP
#define BLOCKHASHES_HPP

#include <map>

#include <csdb/pool.hpp>
#include <lmdb.hpp>

namespace cs {
class BlockHashes {
public:
    explicit BlockHashes(const std::string& path);
    ~BlockHashes() = default;

    bool empty() const {
        return size() == 0;
    }

    size_t size() const;

    void close();
    bool onNextBlock(const csdb::Pool& block);

    csdb::PoolHash find(cs::Sequence seq) const;
    cs::Sequence find(const csdb::PoolHash& hash) const;

    bool remove(cs::Sequence);
    bool remove(const csdb::PoolHash& hash);

private slots:
    void onDbFailed(const cs::LmdbException& exception);

private:
    void initialization();

    cs::Lmdb seqDb_;
    cs::Lmdb hashDb_;
};
}  // namespace cs

#endif  //  BLOCKHASHES_HPP
