#include "csdb/amount.h"

#include <cstdio>
#include <algorithm>

#ifndef _MSC_VER
#define sprintf_s sprintf
#endif

#include "binary_streams.h"

namespace csdb {
Amount::Amount(double value)
{
  if ((value < static_cast<double>(std::numeric_limits<int32_t>::min()))
      || (value > static_cast<double>(std::numeric_limits<int32_t>::max()))) {
      throw std::overflow_error("Amount::Amount(double) overflow)");
  }

  integral_ = static_cast<int32_t>(value);
  if (value < 0.0) {
    --integral_;
  }

  double frac = value - static_cast<double>(integral_);
  uint64_t multiplier = AMOUNT_MAX_FRACTION;
  for (int i = 0; i < std::numeric_limits<double>::digits10; ++i) {
    frac *= 10;
    multiplier /= 10;
  }

  fraction_ = static_cast<uint64_t>(frac + 0.5) * multiplier;
  if (fraction_ >= AMOUNT_MAX_FRACTION) {
    fraction_ -= AMOUNT_MAX_FRACTION;
    ++integral_;
  }
}

::std::string Amount::to_string(size_t min_decimal_places) const noexcept
{
  char buf[64];
  char *end;
  if ((0 > integral_) && (0 != fraction_)) {
    end = sprintf_s(buf, "-%d.%018" PRIu64, (-1) - integral_, AMOUNT_MAX_FRACTION - fraction_) + buf - 1;
  } else {
    end = sprintf_s(buf, "%d.%018" PRIu64, integral_, fraction_) + buf - 1;
  }

  for(min_decimal_places = 18 - ::std::min<size_t>(min_decimal_places, 18);
      min_decimal_places && ('0' == (*end));
      --min_decimal_places, -- end)
  {}

  if ('.' == *end) {
    --end;
  }
  end[1] = '\0';

  return buf;
}

void Amount::put(priv::obstream& os) const
{
  os.put(integral_);
  os.put(fraction_);
}

bool Amount::get(priv::ibstream& is)
{
  return is.get(integral_) && is.get(fraction_);
}

} // namespace csdb
