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
  return d->data_.size() == ::csdb::priv::crypto::public_key_size;
}

bool Address::operator ==(const Address &other) const noexcept
{
  return (d == other.d) || (d->data_ == other.d->data_);
}

bool Address::operator <(const Address &other) const noexcept
{
  return (d != other.d) && (d->data_ < other.d->data_);
}

::std::string Address::to_string() const noexcept
{
  return internal::to_hex(d->data_);
}

Address Address::from_string(const ::std::string &val)
{
  Address res;

  const ::csdb::internal::byte_array data = ::csdb::internal::from_hex( val );
  if (::csdb::priv::crypto::public_key_size == data.size()) {
    res.d->data_ = data;
  }

  return res;
}

::csdb::internal::byte_array Address::public_key() const noexcept
{
  return d->data_;
}

Address Address::from_public_key(const ::csdb::internal::byte_array &key)
{
	Address res;

	if (::csdb::priv::crypto::public_key_size == key.size()) {
		res.d->data_ = key;
	}

	return res;
}

Address Address::from_public_key(const char* key)
{
	Address res;
	res.d->data_ = ::csdb::internal::byte_array((const uint8_t*)key, (const uint8_t*)key + ::csdb::priv::crypto::public_key_size);
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
