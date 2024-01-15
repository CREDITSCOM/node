#ifndef ROUNDSTAT_SERIALIZER_HPP
#define ROUNDSTAT_SERIALIZER_HPP
#include <filesystem>

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/split_member.hpp>

#include <lib/system/common.hpp>


namespace cs {
    class RoundStat;
    class RoundStat_Serializer {
    public:
        void bind(RoundStat& roundStat);
        void save(const std::filesystem::path& rootDir);
        void load(const std::filesystem::path& rootDir);
        void clear(const std::filesystem::path& rootDir);
        void printClassInfo();

        ::cscrypto::Hash hash();

        class Amount {
            friend class boost::serialization::access;
            template<class Archive>
            void serialize(Archive& ar, [[maybe_unused]] const unsigned int version) {
                ar& integral_;
                ar& fraction_;
            }
            static constexpr const uint64_t AMOUNT_MAX_FRACTION = 1000000000000000000ULL;

            int32_t integral_;
            uint64_t fraction_;

        public:
            bool operator<(const Amount& other) const noexcept {
                return (integral_ < other.integral_) ? true : (integral_ > other.integral_) ? false : (fraction_ < other.fraction_);
            }

            bool operator>(const Amount& other) const noexcept {
                return (integral_ > other.integral_) ? true : (integral_ < other.integral_) ? false : (fraction_ > other.fraction_);
            }
            std::string toString(size_t min_decimal_places = 2) {
                char buf[64];
                char* end;
                if ((0 > integral_) && (0 != fraction_)) {
                    end = sprintf_s(buf, "-%d.%018" PRIu64, (-1) - integral_, AMOUNT_MAX_FRACTION - fraction_) + buf - 1;
                }
                else {
                    end = sprintf_s(buf, "%d.%018" PRIu64, integral_, fraction_) + buf - 1;
                }

                for (min_decimal_places = 18 - ::std::min<size_t>(min_decimal_places, 18); (min_decimal_places != 0u) && ('0' == (*end)); --min_decimal_places, --end) {
                }

                if ('.' == *end) {
                    --end;
                }
                end[1] = '\0';

                return buf;
            }
        };

        struct NodeStat {
            friend class boost::serialization::access;
            template<class Archive>
            void serialize(Archive& ar, [[maybe_unused]] const unsigned int archiveVersion);
            std::string toString();

            bool nodeOn;
            std::string ip;
            std::string version;
            std::string platform;
            uint64_t timeReg;
            uint64_t timeFirstConsensus;
            uint64_t timeActive;
            uint64_t trustedDay;
            uint64_t trustedMonth;
            uint64_t trustedPrevMonth;
            uint64_t trustedTotal;
            uint64_t failedTrustedDay;
            uint64_t failedTrustedMonth;
            uint64_t failedTrustedPrevMonth;
            uint64_t failedTrustedTotal;
            uint64_t trustedADay;
            uint64_t trustedAMonth;
            uint64_t trustedAPrevMonth;
            uint64_t trustedATotal;
            uint64_t failedTrustedADay;
            uint64_t failedTrustedAMonth;
            uint64_t failedTrustedAPrevMonth;
            uint64_t failedTrustedATotal;
            Amount feeDay;
            Amount feeMonth;
            Amount feePrevMonth;
            Amount feeTotal;
            Amount rewardDay;
            Amount rewardMonth;
            Amount rewardPrevMonth;
            Amount rewardTotal;
            uint64_t lastConsensus = 0ULL;
        };


        struct MinedEvaluation {
            friend class boost::serialization::access;
            template<class Archive>

            void serialize(Archive& ar, [[maybe_unused]] const unsigned int version) {
                ar& rewardDay;
                ar& rewardMonth;
                ar& rewardPrevMonth;
                ar& rewardTotal;
            }

            Amount rewardDay;
            Amount rewardMonth;
            Amount rewardPrevMonth;
            Amount rewardTotal;
        };

        struct MinedEvaluationDelegator {
            friend class boost::serialization::access;


            template<class Archive>
            void save(Archive& ar, [[maybe_unused]] const unsigned int version) const {
                ar << me;
            }
            template<class Archive>
            void load(Archive& ar, [[maybe_unused]] const unsigned int version) {
                ar >> me;
            }

            BOOST_SERIALIZATION_SPLIT_MEMBER()

            std::map<cs::PublicKey, MinedEvaluation> me;
        };


    private:
        std::map<cs::PublicKey, MinedEvaluationDelegator>* minedEvaluation_ = nullptr;
        std::map<cs::PublicKey, NodeStat>* nodes_ = nullptr;
        MinedEvaluation* totalMined_ = nullptr;
        size_t* totalAcceptedTransactions_ = nullptr;
        size_t totalBchTransactions_ = 0ULL;
    };
}

#endif 