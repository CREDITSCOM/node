/**
 * @file amount_commission.h
 * @author Vladimir Shilkin
 */

#ifndef _CREDITS_CSDB_AMOUNT_COMMISSION_H_INCLUDED_
#define _CREDITS_CSDB_AMOUNT_COMMISSION_H_INCLUDED_

#include <cinttypes>
#include <cmath>
#include <type_traits>

#include <boost/endian/conversion.hpp>

namespace csdb {
class AmountCommission;
}

namespace csdb {
namespace priv {
class obstream;
class ibstream;
}  // namespace priv

#pragma pack(push, 1)
class AmountCommission {
public:
  inline AmountCommission() = default;
  explicit AmountCommission(uint16_t value);
  explicit AmountCommission(double value);

  // Получение значений
public:
  inline double to_double() const noexcept;
  inline explicit operator double() const noexcept {
    return to_double();
  }
  inline uint16_t get_raw() {
    return u_.bits;
  }

  // Сериализация
public:
  void put(priv::obstream&) const;
  bool get(priv::ibstream&);

private:
  union {
    uint16_t bits{};  // All bits
    struct {
#ifdef BOOST_BIG_ENDIAN
      uint16_t sign : 1;   // sign
      uint16_t exp : 5;    // exponent
      uint16_t frac : 10;  // mantissa
#else
      uint16_t frac : 10;  // mantissa
      uint16_t exp : 5;    // exponent
      uint16_t sign : 1;   // sign
#endif
    } fIEEE;
  } u_;
};
#pragma pack(pop)

static_assert(std::is_trivially_copyable<AmountCommission>::value, "Invalid csdb::Amount definition.");

inline double AmountCommission::to_double() const noexcept {
  const double _1_1024 = 1. / 1024;
  return (u_.fIEEE.sign != 0u ? -1. : 1.) * u_.fIEEE.frac * _1_1024 * std::pow(10., u_.fIEEE.exp - 18);
}

}  // namespace csdb

#endif  // _CREDITS_CSDB_AMOUNT_COMMISSION_H_INCLUDED_
