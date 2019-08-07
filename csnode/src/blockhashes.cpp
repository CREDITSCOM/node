#include <csnode/blockhashes.hpp>

#include <conveyer.hpp>
#include <cstring>

#include <lib/system/logger.hpp>

static const char* seqPath = "/seqdb";
static const char* hashPath = "/hashdb";

namespace cs {
BlockHashes::BlockHashes(const std::string& path)
: db_{}
, isDbInited_(false)
, seqDb_(path + seqPath)
, hashDb_(path + hashPath) {
    initialization();
}

void BlockHashes::close() {
    if (seqDb_.isOpen()) {
        seqDb_.close();
    }

    if (hashDb_.isOpen()) {
        hashDb_.close();
    }
}

bool BlockHashes::onNextBlock(const csdb::Pool& block) {
    cs::Sequence seq = block.sequence();

    if (db_.last_ > 0) {
        if (seq <= db_.last_) {
            return false; // already cached
        }
    }

    db_.last_ = seq;

    if (!isDbInited_) {
        db_.first_ = 0;
        isDbInited_ = true;
    }

    auto hash = block.hash();
    auto cachedHash = find(seq);

    if (cachedHash == hash) {
        return true;
    }

    seqDb_.insert(seq, hash.to_binary());
    hashDb_.insert(hash.to_binary(), seq);

    return true;
}

csdb::PoolHash BlockHashes::find(cs::Sequence seq) const {
    if (seq > db_.last_) {
        return csdb::PoolHash{};
    }

    if (seqDb_.size() == 0 || !seqDb_.isKeyExists(seq)) {
        return csdb::PoolHash{};
    }

    auto value = seqDb_.value<cs::Bytes>(seq);
    return csdb::PoolHash::from_binary(std::move(value));
}

cs::Sequence BlockHashes::find(const csdb::PoolHash& hash) const {
    if (seqDb_.size() == 0 || !hashDb_.isKeyExists(hash.to_binary())) {
        return cs::kWrongSequence;
    }

    return hashDb_.value<cs::Sequence>(hash.to_binary());
}

csdb::PoolHash BlockHashes::removeLast() {
    auto [sequence, hash] = seqDb_.last<cs::Sequence, cs::Bytes>();
    seqDb_.remove(sequence);
    hashDb_.remove(hash);

    if (db_.last_ == sequence) {
        --db_.last_;
    }
    else {
        db_.last_ = sequence - 1;
    }

    return csdb::PoolHash::from_binary(std::move(hash));
}

csdb::PoolHash BlockHashes::getLast() {
    if (seqDb_.size() == 0) {
        return csdb::PoolHash{};
    }

    auto pair = seqDb_.last<cs::Sequence, cs::Bytes>();
    return csdb::PoolHash::from_binary(std::move(pair.second));
}

void BlockHashes::onDbFailed(const LmdbException& exception) {
    cswarning() << csfunc() << ", block hashes database exception: " << exception.what();
}

void BlockHashes::initialization() {
    cs::Connector::connect(&seqDb_.failed, this, &BlockHashes::onDbFailed);
    cs::Connector::connect(&hashDb_.failed, this, &BlockHashes::onDbFailed);

    seqDb_.setMapSize(cs::Lmdb::Default1GbMapSize);
    hashDb_.setMapSize(cs::Lmdb::Default1GbMapSize);

    seqDb_.open();
    hashDb_.open();
}
}  // namespace cs
