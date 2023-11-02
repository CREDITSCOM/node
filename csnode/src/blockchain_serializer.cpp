#include <fstream>
#include <sstream>

//#include <boost/archive/text_oarchive.hpp>
//#include <boost/archive/text_iarchive.hpp>

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include <csnode/blockchain.hpp>
#include <csnode/blockchain_serializer.hpp>
#include <csnode/serializers_helper.hpp>

namespace {
const std::string kDataFileName = "blockchain.dat";
} // namespace

namespace cs {

void BlockChain_Serializer::bind(BlockChain& bchain, std::set<cs::PublicKey>& initialConfidants) {
    previousNonEmpty_ = reinterpret_cast<decltype(previousNonEmpty_)>(&bchain.previousNonEmpty_);
    lastNonEmptyBlock_ = reinterpret_cast<decltype(lastNonEmptyBlock_)>(&bchain.lastNonEmptyBlock_);
    totalTransactionsCount_ = &bchain.totalTransactionsCount_;
    uuid_ = &bchain.uuid_;
    lastSequence_ = &bchain.lastSequence_;
    initialConfidants_ = reinterpret_cast<decltype(initialConfidants_)>(&initialConfidants);
    blockRewardIntegral_ = reinterpret_cast<decltype(blockRewardIntegral_)>(&bchain.blockRewardIntegral_);
    blockRewardFraction_ = reinterpret_cast<decltype(blockRewardFraction_)>(&bchain.blockRewardFraction_);
    miningCoefficientIntegral_ = reinterpret_cast<decltype(miningCoefficientIntegral_)>(&bchain.miningCoefficientIntegral_);
    miningCoefficientFraction_ = reinterpret_cast<decltype(miningCoefficientFraction_)>(&bchain.miningCoefficientFraction_);
    stakingOn_ = &bchain.stakingOn_;
    miningOn_ = &bchain.miningOn_;
    TimeMinStage1_ = &bchain.TimeMinStage1_;
    csdebug() << "Blockchain bindings made";
}

void BlockChain_Serializer::clear(const std::filesystem::path& rootDir) {
    previousNonEmpty_->clear();
    lastNonEmptyBlock_->poolSeq = 0;
    lastNonEmptyBlock_->transCount = 0;
    *totalTransactionsCount_ = 0;
    uuid_->store(0);
    lastSequence_->store(0);
    initialConfidants_->clear();
    *blockRewardIntegral_ = 0L;
    *blockRewardFraction_ = 0ULL;
    *miningCoefficientIntegral_= 0L;
    *miningCoefficientFraction_ = 0ULL;
    *stakingOn_ = false;
    *miningOn_ = false;
    *TimeMinStage1_ = 500;
    save(rootDir);
}

void BlockChain_Serializer::save(const std::filesystem::path& rootDir) {
    std::ofstream ofs(rootDir / kDataFileName, std::ios::binary);
    boost::archive::binary_oarchive oa(ofs);
    oa << *previousNonEmpty_;
    oa << *lastNonEmptyBlock_;
    oa << *totalTransactionsCount_;
    oa << uuid_->load();
    oa << lastSequence_->load();
    oa << *initialConfidants_;
    oa << *blockRewardIntegral_ << *blockRewardFraction_;
    oa << *miningCoefficientIntegral_ << *miningCoefficientFraction_;
    uint8_t stakingOn = *stakingOn_ ? 0U : 1U;
    oa << stakingOn;
    uint8_t miningOn = *miningOn_ ? 0U : 1U;
    oa << miningOn;
    oa << *TimeMinStage1_;
}

::cscrypto::Hash BlockChain_Serializer::hash() {
    save(".");
    auto result = SerializersHelper::getHashFromFile(kDataFileName);
    std::filesystem::remove(kDataFileName);
    return result;
}

void BlockChain_Serializer::load(const std::filesystem::path& rootDir) {
    std::ifstream ifs(rootDir / kDataFileName, std::ios::binary);
    boost::archive::binary_iarchive ia(ifs);
    ia >> *previousNonEmpty_;
    ia >> *lastNonEmptyBlock_;
    ia >> *totalTransactionsCount_;

    uint64_t uuid;
    ia >> uuid;
    uuid_->store(uuid);

    Sequence lastSequence;
    ia >> lastSequence;
    lastSequence_->store(lastSequence);
    ia >> *initialConfidants_;

    ia >> *blockRewardIntegral_ >> *blockRewardFraction_;
    ia >> *miningCoefficientIntegral_ >> *miningCoefficientFraction_;
    uint8_t stakingOn;
    uint8_t miningOn;
    ia >> stakingOn;
    ia >> miningOn;
    *stakingOn_ = stakingOn == 0U ? true : false;
    *miningOn_ = miningOn == 0U ? true : false;

    ia >> *TimeMinStage1_;
}
}  // namespace cs
