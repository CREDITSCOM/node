#ifndef BLOCKHASHES_HPP
#define BLOCKHASHES_HPP

#include <csdb/pool.hpp>
#include <vector>

namespace cs {
class BlockHashes {
public:
  struct DbStructure {
    cs::Sequence first_;
    cs::Sequence last_;
  };

public:
  BlockHashes();
  const DbStructure& getDbStructure() const {
    return db_;
  }
  bool empty() const {
    return hashes_.empty();
  }
  void initStart();
  bool initFromPrevBlock(csdb::Pool prevBlock);
  void initFinish();
  bool loadNextBlock(csdb::Pool nextBlock);

  csdb::PoolHash find(cs::Sequence seq) const;

  cs::Sequence find(csdb::PoolHash hash) const;

  csdb::PoolHash removeLast();

  csdb::PoolHash getLast() const;

  const std::vector<csdb::PoolHash>& getHashes() const;

private:
  std::vector<csdb::PoolHash> hashes_;

  DbStructure db_;
  bool isDbInited_;
};
}  // namespace cs

#endif  //  BLOCKHASHES_HPP
