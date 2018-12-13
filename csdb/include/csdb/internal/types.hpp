/**
 * @file types.h
 * @author Roman Bukin
 */

#ifndef _CREDITS_CSDB_TYPES_H_INCLUDED_
#define _CREDITS_CSDB_TYPES_H_INCLUDED_

#include <cinttypes>
#include <string>
#include <vector>

namespace csdb {
namespace internal {

using byte_array = std::vector<std::uint8_t>;
using WalletId = uint32_t;

}  // namespace internal
}  // namespace csdb

#endif  // _CREDITS_CSDB_TYPES_H_INCLUDED_
