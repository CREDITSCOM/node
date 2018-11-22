/**
 * @file types.h
 * @author Roman Bukin
 */

#pragma once
#ifndef _CREDITS_CSDB_TYPES_H_INCLUDED_
#define _CREDITS_CSDB_TYPES_H_INCLUDED_

#include <cinttypes>
#include <string>
#include <vector>

namespace csdb {
namespace internal {

using byte_array = std::vector<std::uint8_t>;
using WalletId = uint32_t;
constexpr size_t kPublicKeySize = 32;
constexpr size_t kPrivateKeySize = 64;
constexpr size_t kSignatureLength = 64;

}  // namespace internal
}  // namespace csdb

#endif  // _CREDITS_CSDB_TYPES_H_INCLUDED_
