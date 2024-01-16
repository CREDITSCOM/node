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
        totalAcceptedTransactions_ = reinterpret_cast<decltype(totalAcceptedTransactions_)>(&roundStat.totalAcceptedTransactions_);
        csdebug() << "Roundstat bindings made";
    }

    void RoundStat_Serializer::clear(const std::filesystem::path& rootDir) {
        minedEvaluation_->clear();
        nodes_->clear();
        totalBchTransactions_ = 0ULL;
        save(rootDir);
    }


    void RoundStat_Serializer::printClassInfo() {
        size_t cnt = 0ULL;
        csdebug() << "Mined:";
        for (auto it : *minedEvaluation_) {
            csdebug() << cnt << ". " << cs::Utils::byteStreamToHex(it.first);
            size_t cntt = 0ULL;
            for (auto itt : it.second.me) {
                csdebug() << cntt << ". " << cs::Utils::byteStreamToHex(itt.first) << " " << itt.second.rewardDay.toString() << " " << itt.second.rewardMonth.toString() << " " << itt.second.rewardPrevMonth.toString() << " " << itt.second.rewardTotal.toString();
                ++cntt;
            }
            ++cnt;
        }
        cnt = 0ULL;
        csdebug() << "Nodes:";
        for (auto it : *nodes_) {
            csdebug() << cnt << ". " << cs::Utils::byteStreamToHex(it.first) << ": " << it.second.toString();
            ++cnt;
        }
        csdebug() << "Total mined: " << totalMined_->rewardDay.toString() << " " << totalMined_->rewardMonth.toString() << " " << totalMined_->rewardPrevMonth.toString() << " " << totalMined_->rewardTotal.toString();

        csdebug() << "Total transactions: " << totalBchTransactions_;

    }

    std::string RoundStat_Serializer::NodeStat::toString() {
        std::string res;
        res += std::string(nodeOn ? "Node ON" : "Node OFF");
        res += ", ip: " + ip;
        res += ", version: " + version;
        res += ", platform: " + platform + ",\n";
        res += "timeReg: " + std::to_string(timeReg);
        res += ", timeFirstConsensus: " + std::to_string(timeFirstConsensus);
        res += ", lastConsensus: " + std::to_string(lastConsensus);
        res += ", timeActive: " + std::to_string(timeActive) + "\n";
        res += "trustedDay: " + std::to_string(trustedDay) + ", trustedMonth: " + std::to_string(trustedMonth)
            + ", trustedPrevMonth: " + std::to_string(trustedPrevMonth) + ", trustedTotal: " + std::to_string(trustedTotal) + "\n";;
        res += "failedTrustedDay: " + std::to_string(failedTrustedDay) + ", failedTrustedMonth: " + std::to_string(failedTrustedMonth)
            + ", failedTrustedPrevMonth: " + std::to_string(failedTrustedPrevMonth) + ", failedTrustedTotal: " + std::to_string(failedTrustedTotal) + "\n";;
        res += "trustedADay: " + std::to_string(trustedADay) + ", trustedAMonth: " + std::to_string(trustedAMonth)
            + ", trustedAPrevMonth: " + std::to_string(trustedAPrevMonth) + ", trustedATotal: " + std::to_string(trustedATotal) + "\n";;
        res += "failedTrustedADay: " + std::to_string(failedTrustedADay) + ", failedTrustedAMonth: " + std::to_string(failedTrustedAMonth)
            + ", failedTrustedAPrevMonth: " + std::to_string(failedTrustedAPrevMonth) + ", failedTrustedAPrevMonth: " + std::to_string(failedTrustedAPrevMonth) + "\n";;
        res += "feeDay: " + feeDay.toString() + ", feeMonth: " + feeMonth.toString()
            + ", failedTrustedPrevMonth: " + feeMonth.toString() + ", failedTrustedTotal: " + feeMonth.toString() + "\n";;
        res += "rewardDay: " + rewardDay.toString() + ", rewardMonth: " + rewardMonth.toString()
            + ", rewardPrevMonth: " + rewardPrevMonth.toString() + ", rewardTotal: " + rewardTotal.toString() + "\n";;

        return res;
    }

    void RoundStat_Serializer::save(const std::filesystem::path& rootDir) {
        std::ofstream ofs(rootDir / kDataFileName, std::ios::binary);
        boost::archive::binary_oarchive oa(ofs);
        csdebug() << kLogPrefix << __func__;
        totalBchTransactions_ = *totalAcceptedTransactions_;
        oa << minedEvaluation_;
        oa << nodes_;
        oa << totalMined_;
        oa << totalBchTransactions_;
        printClassInfo();
    }

    ::cscrypto::Hash RoundStat_Serializer::hash() {
        {
            std::ofstream ofs(kDataFileName, std::ios::binary);
            {
                boost::archive::binary_oarchive oa(
                    ofs,
                    boost::archive::no_header | boost::archive::no_codecvt
                );
                totalBchTransactions_ = *totalAcceptedTransactions_;
                oa << minedEvaluation_;
                oa << nodes_;
                oa << totalMined_;
                oa << totalBchTransactions_;
                printClassInfo();
            }
        }

        auto result = SerializersHelper::getHashFromFile(kDataFileName);
        //std::filesystem::remove(kDataFileName);
        return result;
    }
    template<class Archive>
    void RoundStat_Serializer::NodeStat::serialize(Archive& ar, [[maybe_unused]] const unsigned int archiveVersion) {
        ar& nodeOn;
        ar& ip;
        ar& version;
        ar& platform;
        ar& timeReg;
        ar& timeFirstConsensus;
        ar& timeActive;
        ar& trustedDay;
        ar& trustedMonth;
        ar& trustedPrevMonth;
        ar& trustedTotal;
        ar& failedTrustedDay;
        ar& failedTrustedMonth;
        ar& failedTrustedPrevMonth;
        ar& failedTrustedTotal;
        ar& trustedADay;
        ar& trustedAMonth;
        ar& trustedAPrevMonth;
        ar& trustedATotal;
        ar& failedTrustedADay;
        ar& failedTrustedAMonth;
        ar& failedTrustedAPrevMonth;
        ar& failedTrustedATotal;
        ar& feeDay;
        ar& feeMonth;
        ar& feePrevMonth;
        ar& feeTotal;
        ar& rewardDay;
        ar& rewardMonth;
        ar& rewardPrevMonth;
        ar& rewardTotal;
        ar& lastConsensus;
    }

    void RoundStat_Serializer::load(const std::filesystem::path& rootDir) {
        std::ifstream ifs(rootDir / kDataFileName, std::ios::binary);
        boost::archive::binary_iarchive ia(ifs);
        csdebug() << kLogPrefix << __func__;
        size_t mSize;
        size_t nSize;
        ia >> minedEvaluation_;
        ia >> nodes_;
        ia >> totalMined_;
        ia >> totalBchTransactions_;
        *totalAcceptedTransactions_ = totalBchTransactions_;

        printClassInfo();
    }
}