#include <csnode/blockhashes.hpp>
#include <cstring>
#include <fstream>
#include <lib/system/logger.hpp>

namespace cs {
BlockHashes::BlockHashes()
: db_{}
, isDbInited_(false) {
}

void BlockHashes::initStart() {
}

bool BlockHashes::initFromPrevBlock(csdb::Pool prevBlock) {
  cs::Sequence seq = prevBlock.sequence();
  db_.last_ = seq;
  if (!isDbInited_) {
    db_.first_ = 0;
    db_.last_ = seq;
    hashes_.reserve(db_.last_ + 1);
    isDbInited_ = true;
  }

  hashes_.emplace_back(prevBlock.hash());
  return true;
}

void BlockHashes::initFinish() {
  if (hashes_.size() >= 2) {
    size_t lh = 0;
    size_t rh = hashes_.size() - 1;
    while (lh < rh) {
      std::swap(hashes_[lh++], hashes_[rh--]);
    }
  }

  for (const auto& hash : hashes_) {
    cslog() << "READ> " << hash.to_string();
  }
}

bool BlockHashes::loadNextBlock(csdb::Pool nextBlock) {
  cs::Sequence seq = nextBlock.sequence();
  if (!isDbInited_) {
    db_.first_ = 0;
    db_.last_ = seq;
    isDbInited_ = true;
  }
  else if (seq <= db_.last_) {
    return false;
  }

  if (seq != hashes_.size()) {
    return false;  // see BlockChain::putBlock
  }

  hashes_.emplace_back(nextBlock.hash());
  db_.last_ = seq;
  return true;
}

csdb::PoolHash BlockHashes::find(cs::Sequence seq) const {
  if (empty()) {
    return csdb::PoolHash();
  }
  const auto& range = getDbStructure();
  if (seq < range.first_ || range.last_ < seq) {
    return csdb::PoolHash();
  }
  return hashes_[seq];
}

cs::Sequence BlockHashes::find(csdb::PoolHash hash) const {
  const auto result = std::find(hashes_.cbegin(), hashes_.cend(), hash);

  if (result != hashes_.cend()) {
    return std::distance(hashes_.cbegin(), result);
  }

  return 0;
}

csdb::PoolHash BlockHashes::removeLast() {
  if (hashes_.empty()) {
    return csdb::PoolHash{};
  }
  const auto result = hashes_.back();
  hashes_.pop_back();
  --db_.last_;
  return result;
}

csdb::PoolHash BlockHashes::getLast() const {
  if (hashes_.empty()) {
    return csdb::PoolHash{};
  }
  return hashes_.back();
}

const std::vector<csdb::PoolHash>& BlockHashes::getHashes() const {
  return hashes_;
}

}  // namespace cs
