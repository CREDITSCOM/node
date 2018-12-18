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
  BlockHashes(std::string dbs_fname);
  bool loadDbStructure();
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
  bool saveDbStructure();

  bool find(cs::Sequence seq, csdb::PoolHash& res) const;

private:
  const std::string dbs_fname_;
  std::vector<csdb::PoolHash> hashes_;

  DbStructure db_;
  bool isDbInited_;
};
}  // namespace cs

#endif  //  BLOCKHASHES_HPP
