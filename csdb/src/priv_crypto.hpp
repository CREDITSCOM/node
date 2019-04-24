/**
 * @file priv_crypto.h
 * @author Roman Bukin
 */

#ifndef _CREDITS_CSDB_PRIVATE_CRYPTO_H_H_INCLUDED_
#define _CREDITS_CSDB_PRIVATE_CRYPTO_H_H_INCLUDED_

#include <cinttypes>
#include "csdb/internal/types.hpp"

#ifndef CSDB_UNIT_TEST
#include "cscrypto/cscrypto.hpp"
#endif

namespace csdb {
namespace priv {

struct crypto {
#ifndef CSDB_UNIT_TEST
    static const size_t hash_size = cscrypto::kHashSize;
    static const size_t public_key_size = cscrypto::kPublicKeySize;
#else
    static const size_t hash_size = sizeof(size_t);
    static const size_t public_key_size = 20;
#endif
    static cs::Bytes calc_hash(const cs::Bytes &buffer) noexcept;
};

}  // namespace priv
}  // namespace csdb

#endif  // _CREDITS_CSDB_PRIVATE_CRYPTO_H_H_INCLUDED_
