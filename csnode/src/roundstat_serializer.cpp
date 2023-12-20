#include <fstream>
#include <sstream>
#include <exception>

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include <csnode/roundstat_serializer.hpp>
#include <csnode/serializers_helper.hpp>
#include <csnode/roundstat.hpp>
#include "logger.hpp"

namespace {
    const std::string kDataFileName = "roundstat.dat";
    const std::string kLogPrefix = "RoundStat_Serializer: ";
} // namespace

namespace cs {
    void RoundStat_Serializer::bind(RoundStat& roundStat) {
        minedEvaluation_ = reinterpret_cast<decltype(minedEvaluation_)>(&roundStat.minedEvaluation_);
        nodes_ = reinterpret_cast<decltype(nodes_)>(&roundStat.nodes_);
        totalMined_ = reinterpret_cast<decltype(totalMined_)>(&roundStat.totalMined_);
        totalBlockChainTransactions_ = reinterpret_cast<decltype(totalBlockChainTransactions_)>(&roundStat.totalBlockChainTransactions_);
        csdebug() << "Roundstat bindings made";
    }

    void RoundStat_Serializer::clear(const std::filesystem::path& rootDir) {
        minedEvaluation_->clear();
        nodes_->clear();
        //totalMined_->rewardDay = Amount{ 0 };
        //totalMined_->rewardMonth = Amount{ 0 };
        //totalMined_->rewardPrevMonth = Amount{ 0 };
        //totalMined_->rewardTotal = Amount{ 0 };
        //totalBlockChainTransactions_ = 0ULL;
        save(rootDir);
    }

    void RoundStat_Serializer::save(const std::filesystem::path& rootDir) {
        std::ofstream ofs(rootDir / kDataFileName, std::ios::binary);
        boost::archive::binary_oarchive oa(ofs);
        //csdebug() << kLogPrefix << __func__;
        oa << minedEvaluation_->size();
        for (auto it : *minedEvaluation_) {
            oa << it.first;
            oa << it.second;
        }
        oa << nodes_->size();
        for (auto it : *nodes_) {
            oa << it.first;
            oa << it.second;
        }
        oa << totalMined_->rewardDay << totalMined_->rewardMonth << totalMined_->rewardPrevMonth << totalMined_->rewardTotal;
        //oa << totalBlockChainTransactions_;

    }

    ::cscrypto::Hash RoundStat_Serializer::hash() {
        {
            std::ofstream ofs(kDataFileName, std::ios::binary);
            {
                boost::archive::binary_oarchive oa(
                    ofs,
                    boost::archive::no_header | boost::archive::no_codecvt
                );
                
                csdebug() << kLogPrefix << __func__;
                oa << minedEvaluation_->size();
                for (auto it : *minedEvaluation_) {
                    oa << it.first;
                    oa << it.second;
                }
                oa << nodes_->size();
                for (auto it : *nodes_) {
                    oa << it.first;
                    oa << it.second;
                }
                oa << totalMined_->rewardDay << totalMined_->rewardMonth << totalMined_->rewardPrevMonth << totalMined_->rewardTotal;
                //oa << totalBlockChainTransactions_;
            }
        }

        auto result = SerializersHelper::getHashFromFile(kDataFileName);
        //std::filesystem::remove(kDataFileName);
        return result;
    }

    void RoundStat_Serializer::load(const std::filesystem::path& rootDir) {
        std::ifstream ifs(rootDir / kDataFileName, std::ios::binary);
        boost::archive::binary_iarchive ia(ifs);
        csdebug() << kLogPrefix << __func__;
        size_t mSize;
        size_t nSize;

        ia >> mSize;
        for (int m = 0; m < mSize; ++m) {
            PublicKey pKey;
            MinedEvaluationDelegator md;
            ia >> pKey;
            ia >> md;
            minedEvaluation_->try_emplace(pKey, md);
        }
        ia >> nSize;
        for (int n = 0; n < mSize; ++n) {
            PublicKey pKey;
            NodeStat ns;
            ia >> pKey;
            ia >> ns;
            nodes_->try_emplace(pKey, ns);
        }
        ia >> totalMined_->rewardDay >> totalMined_->rewardMonth >> totalMined_->rewardPrevMonth >> totalMined_->rewardTotal;
        //ia >> totalBlockChainTransactions_;

    }
}