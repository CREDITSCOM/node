#include <boost/functional/hash.hpp>
#include <array>
#include "csdb/address.h"
#include "csdb/internal/types.h"
#include "csdb/internal/utils.h"
#include "csdb/internal/shared_data_ptr_implementation.h"
#include "binary_streams.h"

#include "priv_crypto.h"

namespace csdb {

class Address::priv : public ::csdb::internal::shared_data
{
  union
  {
    std::array<uint8_t, 32> public_key;
    WalletId wallet_id;
  } data_;
  
  bool is_wallet_id_ = false;

  friend class ::csdb::Address;
};
SHARED_DATA_CLASS_IMPLEMENTATION(Address)

bool Address::is_valid() const noexcept
{
  return is_public_key()  ||  is_wallet_id();
}

bool Address::is_public_key() const noexcept
{
  return !d->is_wallet_id_;
}

bool Address::is_wallet_id() const noexcept
{
  return d->is_wallet_id_;
}

bool Address::operator ==(const Address &other) const noexcept
{
  if (d == other.d) {
    return true;
  }
  else if (is_public_key()) {
    if (d->data_.public_key == other.d->data_.public_key) {
      return true;
    }
    else {
      return false;
    }
  }
  else {
    if (d->data_.wallet_id == other.d->data_.wallet_id) {
      return true;
    }
    else {
      return false;
    }
  }
}

bool Address::operator <(const Address &other) const noexcept
{
  if (d == other.d)
    return false;

  if (d->is_wallet_id_ && other.d->is_wallet_id_) {
    return d->data_.wallet_id < other.d->data_.wallet_id;
  }
  else if (!d->is_wallet_id_ && !other.d->is_wallet_id_) {
    return d->data_.public_key < other.d->data_.public_key;
  }
  else if (d->is_wallet_id_ && !other.d->is_wallet_id_) {
    return d->data_.public_key < other.d->data_.public_key;
  }
  else if (!d->is_wallet_id_ && other.d->is_wallet_id_) {
    return d->data_.public_key < other.d->data_.public_key;
  }
  else{
    return false;
  }
}

/*bool Address::operator <(const Address &other) const noexcept
{
  if (d == other.d)
    return false;
  
  if (d->is_wallet_id_ && other.d->is_wallet_id_) {
    return d->data_.wallet_id < other.d->data_.wallet_id;
  } else if (!d->is_wallet_id_ && !other.d->is_wallet_id_) {
    return d->data_.public_key < other.d->data_.public_key;
  } else {
    return false;
  }
}*/

size_t Address::calcHash() const noexcept
{
  if (is_public_key()) {
    return boost::hash_value(d->data_.public_key);
  } else {
    return boost::hash_value(d->data_.wallet_id);
  }
}

::std::string Address::to_string() const noexcept
{
  if (is_public_key())
    return internal::to_hex(::csdb::internal::byte_array(d->data_.public_key.begin(), d->data_.public_key.end()));
  else if (is_wallet_id())
    return std::to_string(wallet_id());
  else
    return std::string();
}

Address Address::from_string(const ::std::string &val)
{
  Address res;

  if (val.size() == 2 * ::csdb::priv::crypto::public_key_size)
  {
      const ::csdb::internal::byte_array data = ::csdb::internal::from_hex(val);
      if (::csdb::priv::crypto::public_key_size == data.size()) {
        memcpy(res.d->data_.public_key.data(), data.data(), ::csdb::priv::crypto::public_key_size);
        res.d->is_wallet_id_ = false;
      }
  }
  else
  {
      try
      {
          if (!val.empty())
          {
              WalletId id = std::stol(val);
              res = from_wallet_id(id);
              res.d->is_wallet_id_ = true;
          }
      }
      catch (...)
      {
      }
  }

  return res;
}

::csdb::internal::byte_array Address::public_key() const noexcept
{
  if (is_public_key())
    return ::csdb::internal::byte_array(d->data_.public_key.begin(), d->data_.public_key.end());
  return {};
}

Address::WalletId Address::wallet_id() const noexcept
{
  if (is_wallet_id())
    return d->data_.wallet_id;
  return static_cast<Address::WalletId>(-1);
}

Address Address::from_public_key(const ::csdb::internal::byte_array &key)
{
	Address res;

  if (::csdb::priv::crypto::public_key_size == key.size()) {
    memcpy(res.d->data_.public_key.data(), key.data(), ::csdb::priv::crypto::public_key_size);
    res.d->is_wallet_id_ = false;
  }

	return res;
}

Address Address::from_public_key(const char* key)
{
  Address res;
  memcpy(res.d->data_.public_key.data(), key, ::csdb::priv::crypto::public_key_size);
  res.d->is_wallet_id_ = false;
  return res;
}

std::string Address::to_api_addr()
{
  return std::string(d->data_.public_key.begin(), d->data_.public_key.end());
}

Address Address::from_wallet_id(WalletId id)
{
  Address res;
  res.d->data_.wallet_id = id;
  res.d->is_wallet_id_ = true;
  return res;
}

void Address::put(::csdb::priv::obstream &os) const
{
  if (is_public_key()) {
    os.put(::csdb::internal::byte_array(d->data_.public_key.begin(), d->data_.public_key.end()));
  } else {
    os.put(d->data_.wallet_id);
  }
  
}

bool Address::get(::csdb::priv::ibstream &is)
{
  if(is.size() == ::csdb::priv::crypto::public_key_size) {
    ::csdb::internal::byte_array tmp;
    bool ok = is.get(tmp);
    memcpy(d->data_.public_key.data(), tmp.data(), ::csdb::priv::crypto::public_key_size);
    return ok;
  } else {
    return is.get(d->data_.wallet_id);
  }
  
}

} // namespace csdb
