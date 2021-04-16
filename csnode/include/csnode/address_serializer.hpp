#ifndef ADDRESS_SERIALIZER_HPP
#define ADDRESS_SERIALIZER_HPP
#include <csdb/address.hpp>

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/split_free.hpp>
#include <boost/serialization/array.hpp>

namespace boost {
namespace serialization {
template<class Archive>
void save(Archive& ar, const csdb::Address& address, [[maybe_unused]] unsigned int version) {
    bool isId = address.is_wallet_id();
    ar & isId;
    if (isId) {
        ar & address.wallet_id();
    } else {
        ar & address.public_key();
    }
}
template<class Archive>
void load(Archive& ar, csdb::Address& address, [[maybe_unused]] unsigned int version) {
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
}
}  // namespace serialization
}  // namespace boost
BOOST_SERIALIZATION_SPLIT_FREE(csdb::Address)
#endif // ADDRESS_SERIALIZER_HPP
