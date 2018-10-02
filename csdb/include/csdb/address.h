/**
  * @file address.h
  * @author Evgeny V. Zalivochkin
  */

#pragma once
#ifndef _CREDITS_CSDB_ADDRESS_H_INCLUDED_
#define _CREDITS_CSDB_ADDRESS_H_INCLUDED_

#include <functional>
#include <string>

#include "csdb/internal/shared_data.h"
#include "csdb/internal/types.h"

namespace csdb {

namespace priv {
class obstream;
class ibstream;
} // namespace priv

class Address
{
  SHARED_DATA_CLASS_DECLARE(Address)
public:
  using WalletId = csdb::internal::WalletId;

  bool is_valid() const noexcept;
  bool is_public_key() const noexcept;
  bool is_wallet_id() const noexcept;
  ::std::string to_string() const noexcept;
  static Address from_string(const std::string &val);

  ::csdb::internal::byte_array public_key() const noexcept;
  // returns (uint32_t)-1 if it is not WalletId
  WalletId wallet_id() const noexcept;

  static Address from_public_key(const ::csdb::internal::byte_array &key);
  static Address from_public_key(const char* key);
  static Address from_wallet_id(WalletId id);

  bool operator ==(const Address &other) const noexcept;
  inline bool operator !=(const Address &other) const noexcept;

  /**
   * @brief operator <
   *
   * Оператор предназначен для возможности сортировок контейнеров класса или
   * использования класса в качестве ключа.
   */
  bool operator < (const Address &other) const noexcept;
  size_t calcHash() const noexcept;

private:
  void put(::csdb::priv::obstream&) const;
  bool get(::csdb::priv::ibstream&);
  friend class ::csdb::priv::obstream;
  friend class ::csdb::priv::ibstream;
};

inline bool Address::operator !=(const Address &other) const noexcept
{
  return !operator ==(other);
}

} // namespace csdb

namespace std
{
template<>
class hash<csdb::Address>
{
 public:
  size_t operator()(const csdb::Address& obj) const {
    return obj.calcHash();
  }
};
} // namespace std

#endif // _CREDITS_CSDB_ADDRESS_H_INCLUDED_
