#include <fstream>
#include <sstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <csnode/blockchain.hpp>
#include <csnode/blockchain_serializer.hpp>
#include <csnode/serializers_helper.hpp>

namespace {
const std::string& kDataFileName = "blockchain.dat";
} // namespace

namespace cs {

void BlockChain_Serializer::bind(BlockChain& bchain) {
    previousNonEmpty_ = reinterpret_cast<decltype(previousNonEmpty_)>(&bchain.previousNonEmpty_);
    lastNonEmptyBlock_ = reinterpret_cast<decltype(lastNonEmptyBlock_)>(&bchain.lastNonEmptyBlock_);
    totalTransactionsCount_ = &bchain.totalTransactionsCount_;
    uuid_ = &bchain.uuid_;
    lastSequence_ = &bchain.lastSequence_;
}

void BlockChain_Serializer::clear(const std::filesystem::path& rootDir) {
    previousNonEmpty_->clear();
    lastNonEmptyBlock_->poolSeq = 0;
    lastNonEmptyBlock_->transCount = 0;
    *totalTransactionsCount_ = 0;
    uuid_->store(0);
    lastSequence_->store(0);
    save(rootDir);
}

void BlockChain_Serializer::save(const std::filesystem::path& rootDir) {
    std::ofstream ofs(rootDir / kDataFileName);
    boost::archive::text_oarchive oa(ofs);
    oa << *previousNonEmpty_;
    oa << *lastNonEmptyBlock_;
    oa << *totalTransactionsCount_;
    oa << uuid_->load();
    oa << lastSequence_->load();
}

::cscrypto::Hash BlockChain_Serializer::hash() {
    save(".");
    auto result = SerializersHelper::getHashFromFile(kDataFileName);
    std::filesystem::remove(kDataFileName);
    return result;
}

void BlockChain_Serializer::load(const std::filesystem::path& rootDir) {
    std::ifstream ifs(rootDir / kDataFileName);
    boost::archive::text_iarchive ia(ifs);
    ia >> *previousNonEmpty_;
    ia >> *lastNonEmptyBlock_;
    ia >> *totalTransactionsCount_;

    uint64_t uuid;
    ia >> uuid;
    uuid_->store(uuid);

    Sequence lastSequence;
    ia >> lastSequence;
    lastSequence_->store(lastSequence);
}
}  // namespace cs
