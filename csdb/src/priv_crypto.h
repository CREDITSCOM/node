/**
  * @file priv_crypto.h
  * @author Roman Bukin
  */

#pragma once
#ifndef _CREDITS_CSDB_PRIVATE_CRYPTO_H_H_INCLUDED_
#define _CREDITS_CSDB_PRIVATE_CRYPTO_H_H_INCLUDED_

#include <cinttypes>
#include "csdb/internal/types.h"

namespace csdb {
namespace priv {

struct crypto
{
  static const size_t hash_size;
  static const size_t public_key_size;
  static internal::byte_array calc_hash(const internal::byte_array &buffer) noexcept;
};

} // namespace priv
} // namespace csdb

#endif // _CREDITS_CSDB_PRIVATE_CRYPTO_H_H_INCLUDED_
