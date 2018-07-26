/**
  * @file math128ce.h
  * @author Evgeny V. Zalivochkin
  * @brief Небольшой набор constexpr-функций и классов для работы со 128-битной арифметикой.
  */

#pragma once
#ifndef _CREDITS_CSDB_INTERNAL_MATH128CE_H_INCLUDED_
#define _CREDITS_CSDB_INTERNAL_MATH128CE_H_INCLUDED_

#include <cinttypes>
#include <type_traits>
#include <stdexcept>

namespace csdb {
namespace internal {

#pragma pack(push, 1)
struct uint128_t
{
  uint64_t lo_ = 0;
  uint64_t hi_ = 0;

  constexpr inline uint128_t() noexcept {}
  constexpr inline uint128_t(const uint64_t lo) noexcept : lo_(lo) {}
  constexpr inline uint128_t(const uint64_t lo, const uint64_t hi) noexcept : lo_(lo), hi_(hi) {}

  constexpr inline uint128_t operator+(const uint64_t summand) const noexcept;
  constexpr inline uint128_t operator+(const uint128_t& summand) const noexcept;

  // Умножение
public:
  static constexpr inline uint128_t mul(uint64_t a, uint64_t b);
private:
  static constexpr inline uint128_t mul1(const uint64_t a, const uint64_t b, const uint64_t c, const uint64_t d);
  static constexpr inline uint128_t mul2(const uint64_t ac, const uint64_t ad, const uint64_t bc, const uint64_t bd);

  // Деление
public:
  struct division64_result;
  constexpr inline division64_result div(const uint64_t divisor) const;
private:
  constexpr inline division64_result div1(const division64_result& previous, const uint64_t divisor,
                                          const size_t shift) const;
  constexpr inline division64_result div2(const division64_result& previous, const uint64_t divisor,
                                          const size_t shift) const;
  constexpr inline division64_result div3(const division64_result& previous, const uint64_t divisor,
                                          const uint64_t bit, const uint64_t dividend) const;
};

struct uint128_t::division64_result
{
  uint128_t quotient_;
  uint64_t remainder_;
};
#pragma pack(pop)

} // namespace internal
} // namespace csdb

constexpr inline
csdb::internal::uint128_t
csdb::internal::uint128_t::operator+(const uint64_t summand) const noexcept
{
  return ((0xFFFFFFFFFFFFFFFFULL - lo_) < summand)
      ? uint128_t(lo_ - (0xFFFFFFFFFFFFFFFFULL - summand + 1), hi_ + 1)
      : csdb::internal::uint128_t(lo_ + summand, hi_);
}

constexpr inline
csdb::internal::uint128_t
csdb::internal::uint128_t::operator+(const uint128_t& summand) const noexcept
{
  return ((0xFFFFFFFFFFFFFFFFULL - lo_) < summand.lo_)
      ? uint128_t(lo_ - (0xFFFFFFFFFFFFFFFFULL - summand.lo_ + 1), hi_ + summand.hi_ + 1)
      : csdb::internal::uint128_t(lo_ + summand.lo_, hi_ + summand.hi_);
}

constexpr inline
csdb::internal::uint128_t
csdb::internal::uint128_t::mul(uint64_t a, uint64_t b)
{
  return mul1(a >> 32, a & 0xFFFFFFFFUL, b >> 32, b & 0xFFFFFFFFUL);
}

constexpr inline
csdb::internal::uint128_t
csdb::internal::uint128_t::mul1(const uint64_t a, const uint64_t b, const uint64_t c, const uint64_t d)
{
  return mul2(a * c, a * d, b * c, b * d);
}

constexpr inline
csdb::internal::uint128_t
csdb::internal::uint128_t::mul2(const uint64_t ac, const uint64_t ad, const uint64_t bc, const uint64_t bd)
{
  return uint128_t(bd, ac + (ad >> 32) + (bc >> 32)) + (ad << 32) + (bc << 32);
}

constexpr inline
csdb::internal::uint128_t::division64_result
csdb::internal::uint128_t::div(const uint64_t divisor) const
{
  return
      (0ULL == divisor) ? throw std::overflow_error("Division by zero") :
      (1ULL == divisor) ? division64_result{*this, 0} :
                          div1(division64_result{{0, hi_ / divisor}, hi_ % divisor}, divisor, 63);
}

constexpr inline
csdb::internal::uint128_t::division64_result
csdb::internal::uint128_t::div1(const division64_result& previous, const uint64_t divisor, const size_t shift) const
{
  return (0 == shift)
      ? div2(previous, divisor, shift)
      : div1(div2(previous, divisor, shift), divisor, shift - 1);
}

constexpr inline
csdb::internal::uint128_t::division64_result
csdb::internal::uint128_t::div2(const division64_result& previous, const uint64_t divisor, const size_t shift) const
{
  return div3(previous, divisor, 1ULL << shift, (previous.remainder_ << 1) + ((lo_ >> shift) & 0x1));
}

constexpr inline
csdb::internal::uint128_t::division64_result
csdb::internal::uint128_t::div3(const division64_result& previous, const uint64_t divisor, const uint64_t bit,
                                const uint64_t dividend) const
{
  return (0 == (0x8000000000000000ULL & previous.remainder_))
      ? division64_result{{previous.quotient_.lo_ + ((dividend < divisor) ? 0 : bit), previous.quotient_.hi_},
                          dividend % divisor}
      : division64_result{{previous.quotient_.lo_ + bit, previous.quotient_.hi_},
                          0xFFFFFFFFFFFFFFFF - divisor + dividend + 1};
}

static_assert(std::is_trivially_copyable<csdb::internal::uint128_t>::value,
              "Invalid csdb::internal::uint128_t definition.");
static_assert(sizeof(csdb::internal::uint128_t) == 16,
              "Invalid size of csdb::internal::uint128_t.");

#endif // _CREDITS_CSDB_INTERNAL_MATH128CE_H_INCLUDED_
