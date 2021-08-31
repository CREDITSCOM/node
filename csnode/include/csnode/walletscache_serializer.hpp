#ifndef WALLETS_CACHE_SERIALIZER_HPP
#define WALLETS_CACHE_SERIALIZER_HPP
#include <filesystem>

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/split_member.hpp>

#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

#include <lib/system/common.hpp>
#include <lib/system/serialize_tuple.hpp>

#include <csnode/transactionstail.hpp>
#include <cscrypto/cscrypto.hpp>

#include "address_serializer.hpp"

using namespace boost::multi_index;

namespace cs {
class WalletsCache;

class WalletsCache_Serializer {
public:
    void bind(WalletsCache&);
    void save(const std::filesystem::path& rootDir);
    void load(const std::filesystem::path& rootDir);
    void clear(const std::filesystem::path& rootDir);

    ::cscrypto::Hash hash();

private:
#pragma pack(push, 1)
    class Amount {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & integral_;
            ar & fraction_;
        }

        int32_t integral_;
        uint64_t fraction_;

    public:
        bool operator<(const Amount& other) const noexcept {
            return (integral_ < other.integral_) ? true : (integral_ > other.integral_) ? false : (fraction_ < other.fraction_);
        }

        bool operator>(const Amount& other) const noexcept {
            return (integral_ > other.integral_) ? true : (integral_ < other.integral_) ? false : (fraction_ > other.fraction_);
        }
    };
#pragma pack(pop)
    class TransactionID {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & pool_seq_;
            ar & index_;
        }

        cs::Sequence pool_seq_;
        cs::Sequence index_;
    };

    class TimeMoney {
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & time;
            ar & amount;
        }

        uint64_t time;
        Amount amount;
    };

    class WalletData {
        friend class boost::serialization::access;
        template<class Archive>
        void save(Archive &ar, [[maybe_unused]] const unsigned int version) const {
            ar & key_;
            ar & balance_;
            ar & delegated_;

            bool hasDelegateSources(delegateSources_);
            ar & hasDelegateSources;
            if (hasDelegateSources) {
                ar & *delegateSources_;
            }

            bool hasDelegateTargets(delegateTargets_);
            ar & hasDelegateTargets;
            if (hasDelegateTargets) {
                ar & *delegateTargets_;
            }

            ar & trxTail_;
            ar & transNum_;
            ar & lastTransaction_;
#ifdef MONITOR_NODE
            ar & createTime_;
#endif
        }

        template<class Archive>
        void load(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & key_;
            ar & balance_;
            ar & delegated_;

            bool hasDelegateSources;
            ar & hasDelegateSources;
            if (hasDelegateSources) {
                decltype(delegateSources_)::element_type tmp;
                ar & tmp;
                delegateSources_ = std::make_shared<decltype(tmp)>(tmp);
            }

            bool hasDelegateTargets;
            ar & hasDelegateTargets;
            if (hasDelegateTargets) {
                decltype(delegateTargets_)::element_type tmp;
                ar & tmp;
                delegateTargets_ = std::make_shared<decltype(tmp)>(tmp);
            }

            ar & trxTail_;
            ar & transNum_;
            ar & lastTransaction_;
#ifdef MONITOR_NODE
            ar & createTime_;
#endif
        }

        BOOST_SERIALIZATION_SPLIT_MEMBER()

    public:
        PublicKey key_;
        Amount balance_;
        Amount delegated_;
        std::shared_ptr<std::map<PublicKey, std::vector<TimeMoney>>> delegateSources_;
        std::shared_ptr<std::map<PublicKey, std::vector<TimeMoney>>> delegateTargets_;
        TransactionsTail trxTail_;
        uint64_t transNum_;
        TransactionID lastTransaction_;
#ifdef MONITOR_NODE
        uint64_t createTime_;
#endif
    };
#ifdef MONITOR_NODE
    class TrustedData {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & times;
            ar & times_trusted;
            ar & totalFee;
        }

        uint64_t times;
        uint64_t times_trusted;
        Amount totalFee;
    };
#endif

    using Delegations = std::vector<std::tuple<
        PublicKey,
        PublicKey,
        TransactionID>
    >;

    using CurrentDelegations = std::map<uint64_t, Delegations>;

    using MiningDelegations = std::unordered_map<
        PublicKey,
        std::vector<std::pair<PublicKey, TimeMoney>>
    >;

    std::list<TransactionID> *smartPayableTransactions_ = nullptr;
    std::map<csdb::Address, std::list<TransactionID>> *canceledSmarts_ = nullptr;

    using MultiWalletsContainer = boost::multi_index_container<
        WalletData,
        indexed_by<
            hashed_unique<member<WalletData, PublicKey, &WalletData::key_>>,
            ordered_non_unique<member<WalletData, Amount, &WalletData::balance_>, std::greater<Amount>>,
            ordered_non_unique<member<WalletData, uint64_t, &WalletData::transNum_>, std::greater<uint64_t>>,
#ifdef MONITOR_NODE
            ordered_non_unique<member<WalletData, uint64_t, &WalletData::createTime_>, std::greater<uint64_t>>,
#endif
            ordered_unique<member<WalletData, PublicKey, &WalletData::key_>>
        >
    >;

    MultiWalletsContainer *wallets_ = nullptr;
#ifdef MONITOR_NODE
    std::map<PublicKey, TrustedData> *trusted_info_ = nullptr;
#endif
    CurrentDelegations *currentDelegations_ = nullptr;
    MiningDelegations *miningDelegations_ = nullptr;
};
} // namespace cs
#endif // WALLETS_CACHE_SERIALIZER_HPP
