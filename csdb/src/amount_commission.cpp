#include <cmath>

#include <csdb/amount_commission.h>

#include "binary_streams.h"

namespace csdb {

AmountCommission::AmountCommission(uint16_t value)
: bits_(value) {
}

AmountCommission::AmountCommission(double value) {
  fIEEE_.sign = value < 0. ? 1 : 0;
  value = std::fabs(value);
  double expf = value == 0. ? 0. : std::log10(value);
  int expi = expf >= 0. ? expf + 0.5 : expf - 0.5;
  value /= std::pow(10, expi);
  if (value >= 1.) {
    value *= 0.1;
    ++expi;
  }
  fIEEE_.exp = expi + 18;
  fIEEE_.frac = value * 1024 + 0.5;
}

void AmountCommission::put(priv::obstream& os) const {
  os.put(bits_);
}

bool AmountCommission::get(priv::ibstream& is) {
  return is.get(bits_);
}

}  // namespace csdb
