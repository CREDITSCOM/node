#include <csnode/blockhashes.hpp>

#include <conveyer.hpp>
#include <cstring>

#include <lib/system/logger.hpp>

static const char* seqPath = "/seqdb";
static const char* hashPath = "/hashdb";

namespace cs {
BlockHashes::BlockHashes(const std::string& path)
: seqDb_(path + seqPath)
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

size_t BlockHashes::size() const {
    return seqDb_.size();
}

bool BlockHashes::onNextBlock(const csdb::Pool& block) {
    cs::Sequence seq = block.sequence();

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
    if (seqDb_.size() == 0 || !seqDb_.isKeyExists(seq)) {
        return csdb::PoolHash{};
    }

    auto value = seqDb_.value<cs::Bytes>(seq);
    return csdb::PoolHash::from_binary(std::move(value));
}

cs::Sequence BlockHashes::find(const csdb::PoolHash& hash) const {
    if (hashDb_.size() == 0 || !hashDb_.isKeyExists(hash.to_binary())) {
        return cs::kWrongSequence;
    }

    return hashDb_.value<cs::Sequence>(hash.to_binary());
}

bool BlockHashes::remove(cs::Sequence sequence) {
    auto hash = find(sequence);
    if (hash.is_empty()) {
        return false;
    }
    seqDb_.remove(sequence);
    hashDb_.remove(hash.to_binary());
    return true;
}

bool BlockHashes::remove(const csdb::PoolHash& hash) {
    auto sequence = find(hash);
    if (sequence == kWrongSequence) {
        return false;
    }
    seqDb_.remove(sequence);
    hashDb_.remove(hash.to_binary());
    return true;
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
