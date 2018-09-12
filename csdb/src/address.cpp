#include <boost/functional/hash.hpp>
#include "csdb/address.h"
#include "csdb/internal/types.h"
#include "csdb/internal/utils.h"
#include "csdb/internal/shared_data_ptr_implementation.h"
#include "binary_streams.h"

#include "priv_crypto.h"

namespace csdb {

class Address::priv : public ::csdb::internal::shared_data
{
  ::csdb::internal::byte_array data_;

  friend class ::csdb::Address;
};
SHARED_DATA_CLASS_IMPLEMENTATION(Address)

bool Address::is_valid() const noexcept
{
    return is_public_key()  ||  is_wallet_id();
}

bool Address::is_public_key() const noexcept
{
    return d->data_.size() == ::csdb::priv::crypto::public_key_size;
}

bool Address::is_wallet_id() const noexcept
{
    return d->data_.size() == sizeof(WalletId);
}

bool Address::operator ==(const Address &other) const noexcept
{
  return (d == other.d) || (d->data_ == other.d->data_);
}

bool Address::operator <(const Address &other) const noexcept
{
  return (d != other.d) && (d->data_ < other.d->data_);
}

size_t Address::calcHash() const noexcept
{
    return boost::hash_value(d->data_);
}

::std::string Address::to_string() const noexcept
{
    if (is_public_key())
        return internal::to_hex(d->data_);
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
          res.d->data_ = data;
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
        return d->data_;
    return {};
}

Address::WalletId Address::wallet_id() const noexcept
{
    if (is_wallet_id())
        return *reinterpret_cast<const WalletId*>(&d->data_[0]);
    return static_cast<Address::WalletId>(-1);
}

Address Address::from_public_key(const ::csdb::internal::byte_array &key)
{
	Address res;

	if (::csdb::priv::crypto::public_key_size == key.size())
		res.d->data_ = key;

	return res;
}

Address Address::from_public_key(const char* key)
{
    Address res;
    res.d->data_ = ::csdb::internal::byte_array((const uint8_t*)key, (const uint8_t*)key + ::csdb::priv::crypto::public_key_size);
    return res;
}

Address Address::from_wallet_id(WalletId id)
{
    Address res;
    res.d->data_.assign((uint8_t*)&id, (uint8_t*)&id + sizeof(id));
    return res;
}

void Address::put(::csdb::priv::obstream &os) const
{
  os.put(d->data_);
}

bool Address::get(::csdb::priv::ibstream &is)
{
  return is.get(d->data_);
}

} // namespace csdb
