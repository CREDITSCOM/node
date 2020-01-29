#ifndef WALLETS_CACHE_SERIALIZER_HPP
#define WALLETS_CACHE_SERIALIZER_HPP
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/map.hpp>

#include "address_serializer.hpp"

namespace cs {
class WalletsCache;

class WalletsCache_Serializer {
public:
    void bind(WalletsCache&);
    void save();
    void load();

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

    class WalletData {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & balance_;
            ar & delegated_;
            ar & delegats_;
            ar & trxTail_;
            ar & transNum_;
            ar & lastTransaction_;
#ifdef MONITOR_NODE
            ar & createTime_;
#endif
        }

        Amount balance_;
        Amount delegated_;
        std::map<cs::PublicKey, Amount> delegats_;
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
    std::list<TransactionID> *smartPayableTransactions_ = nullptr;
    std::map<csdb::Address, std::list<TransactionID>> *canceledSmarts_ = nullptr;
    std::unordered_map<PublicKey, WalletData> *wallets_ = nullptr;
#ifdef MONITOR_NODE
    std::map<PublicKey, TrustedData> *trusted_info_ = nullptr;
#endif
};
} // namespace cs
#endif // WALLETS_CACHE_SERIALIZER_HPP
