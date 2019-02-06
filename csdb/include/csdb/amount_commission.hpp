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
  10e-18, 10e-17, 10e-16, 10e-15, 10e-14, 10e-13, 10e-12, 10e-11,
  10e-10,  10e-9,  10e-8,  10e-7,  10e-6,  10e-5,  10e-4,  10e-3,
   10e-2,  10e-1,     1.,    10.,   10e2,   10e3,   10e4,   10e5,
    10e6,   10e7,   10e8,   10e9,  10e10,  10e11,  10e12,  10e13 };
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
