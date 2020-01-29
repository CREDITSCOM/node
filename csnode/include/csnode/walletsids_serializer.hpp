#ifndef WALLET_IDS_SERIALIZER_HPP
#define WALLET_IDS_SERIALIZER_HPP
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/set.hpp>

namespace cs {
class WalletsIds;

class WalletsIds_Serializer {
public:
    void bind(WalletsIds&);
    void save();
    void load();

private:
    using WalletId = csdb::internal::WalletId;
    using WalletAddress = csdb::Address;

    class Wallet {
        friend class boost::serialization::access;
        template<class Archive>
        void save(Archive& ar, [[maybe_unused]] const unsigned int version) const {
            bool isId = address.is_wallet_id();
            ar & isId;
            if (isId) {
              ar & address.wallet_id();
            } else {
              ar & address.public_key();
            }
            ar & id;
        }
        template<class Archive>
        void load(Archive& ar, [[maybe_unused]] const unsigned int version) {
            bool isId;
            ar & isId;
            if (isId) {
              csdb::Address::WalletId wId;
              ar & wId;
              address = csdb::Address::from_wallet_id(wId);
            } else {
              cs::PublicKey pKey;
              ar & pKey;
              address = csdb::Address::from_public_key(pKey);
            }
            ar & id;
        }
        BOOST_SERIALIZATION_SPLIT_MEMBER()

    public:
        WalletAddress address; struct byAddress {};
        WalletId id;  struct byId {};
    };

    using Data = multi_index_container<
        Wallet,
        indexed_by<
            hashed_unique<
                tag<Wallet::byAddress>, member<
                    Wallet, WalletAddress, &Wallet::address
                >
            >,
            hashed_unique<
                tag<Wallet::byId>, member<
                    Wallet, WalletId, &Wallet::id
                >
            >
        >
    >;

    Data *data_ = nullptr;
    WalletId *nextId_ = nullptr;
};
}  // namespace cs
#endif // WALLET_IDS_SERIALIZER_HPP
