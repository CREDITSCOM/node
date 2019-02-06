/**
 * @file amount_commission.h
 * @author Vladimir Shilkin
 */

#ifndef _CREDITS_CSDB_AMOUNT_COMMISSION_H_INCLUDED_
#define _CREDITS_CSDB_AMOUNT_COMMISSION_H_INCLUDED_

#include <cinttypes>
#include <type_traits>
#include <atomic>

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
  inline AmountCommission(const AmountCommission& other) {
    *this = other;
  }
  inline csdb::AmountCommission& operator=(const AmountCommission& other) {
    u_ = other.u_;
    return *this;
  }

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

  mutable double cachedDouble_;
  mutable std::atomic_bool cached_ = false;
};
#pragma pack(pop)

namespace {
double tens_pows[32] = {
  1e-18, 1e-17, 1e-16, 1e-15, 1e-14, 1e-13, 1e-12, 1e-11,
  1e-10,  1e-9,  1e-8,  1e-7,  1e-6,  1e-5,  1e-4,  1e-3,
   1e-2,  1e-1,    1.,   1e1,   1e2,   1e3,   1e4,   1e5,
    1e6,   1e7,   1e8,   1e9,  1e10,  1e11,  1e12,  1e13 };
}  // anonymous namspace

inline double AmountCommission::to_double() const noexcept {
  if (cached_.load(std::memory_order_relaxed)) return cachedDouble_;
  const double _1_1024 = 1. / 1024;
  cachedDouble_ = (u_.fIEEE.sign != 0u ? -1. : 1.) * u_.fIEEE.frac * _1_1024 * tens_pows[u_.fIEEE.exp];
  cached_.store(true, std::memory_order_release);
  return cachedDouble_;
}

}  // namespace csdb

#endif  // _CREDITS_CSDB_AMOUNT_COMMISSION_H_INCLUDED_
