/**
  * @file types.h
  * @author Roman Bukin
  */

#pragma once
#ifndef _CREDITS_CSDB_TYPES_H_INCLUDED_
#define _CREDITS_CSDB_TYPES_H_INCLUDED_

#include <cinttypes>
#include <vector>
#include <string>

namespace csdb {
namespace internal {

using byte_array = std::vector<std::uint8_t>;
using WalletId = uint32_t;

} // namespace internal
} // namespace csdb

#endif // _CREDITS_CSDB_TYPES_H_INCLUDED_
