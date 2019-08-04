#include <csnode/blockhashes.hpp>

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
    //initialization();
}

bool BlockHashes::initFromPrevBlock(csdb::Pool prevBlock) {
    cs::Sequence seq = prevBlock.sequence();
    db_.last_ = seq;

    if (!isDbInited_) {
        db_.first_ = 0;
        isDbInited_ = true;
    }

    auto hash = prevBlock.hash();

    //auto cached_hash = find(seq);
    //if (cached_hash == hash) {
    //    return true;
    //}

    //seqDb_.insert(seq, hash.to_binary());
    //hashDb_.insert(hash.to_binary(), seq);

    return true;
}

bool BlockHashes::loadNextBlock(csdb::Pool nextBlock) {
    cs::Sequence seq = nextBlock.sequence();

    if (!isDbInited_) {
        db_.first_ = 0;
        isDbInited_ = true;
    }
    else if (seq <= db_.last_) {
        csdebug() << csfunc() << ": seq <= db_.last_";
        return false;
    }

    //if (seq != seqDb_.size()) {
    //    csdebug() << csfunc() << ": seq != hashes_.size()";
    //    return false;  // see BlockChain::putBlock
    //}

    //auto hash = nextBlock.hash();

    //seqDb_.insert(seq, hash.to_binary());
    //hashDb_.insert(hash.to_binary(), seq);

    db_.last_ = seq;

    return true;
}

csdb::PoolHash BlockHashes::find(cs::Sequence seq) const {
    if (empty() || !seqDb_.isKeyExists(seq)) {
        return csdb::PoolHash{};
    }

    auto value = seqDb_.value<cs::Bytes>(seq);
    return csdb::PoolHash::from_binary(std::move(value));
}

cs::Sequence BlockHashes::find(csdb::PoolHash hash) const {
    if (empty() || !hashDb_.isKeyExists(hash.to_binary())) {
        return cs::kWrongSequence;
    }

    return hashDb_.value<cs::Sequence>(hash.to_binary());
}

csdb::PoolHash BlockHashes::removeLast() {
    if (empty()) {
        return csdb::PoolHash{};
    }

    auto pair = seqDb_.last<cs::Sequence, cs::Bytes>();

    //seqDb_.remove(pair.first);
    //hashDb_.remove(pair.second);

    if (db_.last_ == pair.first) {
        --db_.last_;
    }
    else {
        db_.last_ = pair.first - 1;
    }

    return csdb::PoolHash::from_binary(std::move(pair.second));
}

csdb::PoolHash BlockHashes::getLast() const {
    //if (empty()) {
        return csdb::PoolHash{};
    //}

    //auto pair = seqDb_.last<cs::Sequence, cs::Bytes>();
    //return csdb::PoolHash::from_binary(std::move(pair.second));
}

void BlockHashes::onDbFailed(const LmdbException& exception) {
    cswarning() << csfunc() << ", block hashes database exception: " << exception.what();
}

void BlockHashes::initialization() {
    cs::Connector::connect(&seqDb_.failed, this, &BlockHashes::onDbFailed);
    cs::Connector::connect(&hashDb_.failed, this, &BlockHashes::onDbFailed);

    seqDb_.setMapSize(cs::Lmdb::DefaultMapSize);
    hashDb_.setMapSize(cs::Lmdb::DefaultMapSize);

    seqDb_.open();
    hashDb_.open();
}
}  // namespace cs
