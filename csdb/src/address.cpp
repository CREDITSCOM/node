#include "csdb/address.hpp"
#include <array>
#include <boost/functional/hash.hpp>
#include "binary_streams.hpp"
#include "csdb/internal/shared_data_ptr_implementation.hpp"
#include "csdb/internal/types.hpp"
#include "csdb/internal/utils.hpp"

#include "priv_crypto.hpp"

namespace csdb {

class Address::priv : public ::csdb::internal::shared_data {
  union {
    cs::PublicKey public_key;
    WalletId wallet_id;
  } data_{};

  bool is_wallet_id_ = false;

  DEFAULT_PRIV_CLONE()

  friend class ::csdb::Address;
};
SHARED_DATA_CLASS_IMPLEMENTATION(Address)

bool Address::is_valid() const noexcept {
  return is_public_key() || is_wallet_id();
}

bool Address::is_public_key() const noexcept {
  return !d->is_wallet_id_;
}

bool Address::is_wallet_id() const noexcept {
  return d->is_wallet_id_;
}

bool Address::operator==(const Address &other) const noexcept {
  if (d == other.d) {
    return true;
  }
  if (is_public_key()) {
    return d->data_.public_key == other.d->data_.public_key;
  }
  return d->data_.wallet_id == other.d->data_.wallet_id;
}

bool Address::operator<(const Address &other) const noexcept {
  if (d == other.d) {
    return false;
  }

  if (d->is_wallet_id_ && other.d->is_wallet_id_) {
    return d->data_.wallet_id < other.d->data_.wallet_id;
  }
  if (!d->is_wallet_id_ && !other.d->is_wallet_id_) {
    return d->data_.public_key < other.d->data_.public_key;
  }
  if (d->is_wallet_id_ && !other.d->is_wallet_id_) {
    return d->data_.public_key < other.d->data_.public_key;
  }
  if (!d->is_wallet_id_ && other.d->is_wallet_id_) {
    return d->data_.public_key < other.d->data_.public_key;
  }
  return false;
}

size_t Address::calcHash() const noexcept {
  if (is_public_key()) {
    return boost::hash_value(d->data_.public_key);
  }
  return boost::hash_value(d->data_.wallet_id);
}

::std::string Address::to_string() const noexcept {
  if (is_public_key()) {
    return internal::to_hex(cs::Bytes(d->data_.public_key.begin(), d->data_.public_key.end()));
  }
  if (is_wallet_id()) {
    return std::to_string(wallet_id());
  }
  return std::string();
}

Address Address::from_string(const ::std::string &val) {
  Address res;

  if (val.size() == 2 * ::csdb::priv::crypto::public_key_size) {
    const cs::Bytes data = ::csdb::internal::from_hex(val);
    if (::csdb::priv::crypto::public_key_size == data.size()) {
      memcpy(res.d->data_.public_key.data(), data.data(), ::csdb::priv::crypto::public_key_size);
      res.d->is_wallet_id_ = false;
    }
  }
  else {
    try {
      if (!val.empty()) {
        WalletId id = std::stol(val);
        res = from_wallet_id(id);
        res.d->is_wallet_id_ = true;
      }
    }
    catch (...) {
    }
  }

  return res;
}

const cs::PublicKey& Address::public_key() const noexcept {
  return d->data_.public_key;
}

Address::WalletId Address::wallet_id() const noexcept {
  if (is_wallet_id()) {
    return d->data_.wallet_id;
  }
  return static_cast<Address::WalletId>(-1);
}

Address Address::from_public_key(const cs::Bytes& key) {
  Address res;

  if (::csdb::priv::crypto::public_key_size == key.size()) {
    std::copy(key.begin(), key.end(), res.d->data_.public_key.data());
    res.d->is_wallet_id_ = false;
  }

  return res;
}

Address Address::from_public_key(const cs::PublicKey& key) {
  Address res;
  res.d->data_.public_key = key;
  res.d->is_wallet_id_ = false;

  return res;
}

Address Address::from_public_key(const char *key) {
  Address res;
  std::copy(key, key + ::csdb::priv::crypto::public_key_size, res.d->data_.public_key.begin());
  res.d->is_wallet_id_ = false;
  return res;
}

std::string Address::to_api_addr() {
  return std::string(d->data_.public_key.begin(), d->data_.public_key.end());
}

Address Address::from_wallet_id(WalletId id) {
  Address res;
  res.d->data_.wallet_id = id;
  res.d->is_wallet_id_ = true;
  return res;
}

void Address::put(::csdb::priv::obstream& os) const {
  if (is_public_key()) {
    os.put(d->data_.public_key);
  }
  else {
    os.put(d->data_.wallet_id);
  }
}

bool Address::get(::csdb::priv::ibstream& is) {
  if (is.size() == ::csdb::priv::crypto::public_key_size) {
    bool ok = is.get(d->data_.public_key);
    return ok;
  }
  return is.get(d->data_.wallet_id);
}

}  // namespace csdb
