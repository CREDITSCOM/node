/**
  * @file priv_crypto.h
  * @author Roman Bukin
  */

#pragma once
#ifndef _CREDITS_CSDB_PRIVATE_CRYPTO_H_H_INCLUDED_
#define _CREDITS_CSDB_PRIVATE_CRYPTO_H_H_INCLUDED_

#include "csdb/internal/types.h"

#ifndef CSDB_UNIT_TEST
#include "cscrypto/cscrypto.h"
#endif

namespace csdb {
namespace priv {

struct crypto
{
#ifndef CSDB_UNIT_TEST
  static const size_t hash_size = cscrypto::Hash::sizeBytes;
  static const size_t public_key_size = cscrypto::PublicKey::sizeBytes;
#else
  static const size_t hash_size = sizeof(size_t);
  static const size_t public_key_size = 20;
#endif
  static internal::byte_array calc_hash(const internal::byte_array &buffer) noexcept;
};

} // namespace priv
} // namespace csdb

#endif // _CREDITS_CSDB_PRIVATE_CRYPTO_H_H_INCLUDED_
