#include <fstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <csnode/blockchain.hpp>
#include <csnode/blockchain_serializer.hpp>

namespace cs {
void BlockChain_Serializer::bind(BlockChain& bchain) {
    previousNonEmpty_ = reinterpret_cast<decltype(previousNonEmpty_)>(&bchain.previousNonEmpty_);
    lastNonEmptyBlock_ = reinterpret_cast<decltype(lastNonEmptyBlock_)>(&bchain.lastNonEmptyBlock_);
    totalTransactionsCount_ = &bchain.totalTransactionsCount_;
    uuid_ = &bchain.uuid_;
    lastSequence_ = &bchain.lastSequence_;
}

void BlockChain_Serializer::clear() {
    previousNonEmpty_->clear();
    lastNonEmptyBlock_->poolSeq = 0;
    lastNonEmptyBlock_->transCount = 0;
    *totalTransactionsCount_ = 0;
    uuid_->store(0);
    lastSequence_->store(0);
    save();
}

void BlockChain_Serializer::save() {
    std::ofstream ofs("blockchain.dat");
    boost::archive::text_oarchive oa(ofs);
    oa << *previousNonEmpty_;
    oa << *lastNonEmptyBlock_;
    oa << *totalTransactionsCount_;
    oa << uuid_->load();
    oa << lastSequence_->load();
}

void BlockChain_Serializer::load() {
    std::ifstream ifs("blockchain.dat");
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
