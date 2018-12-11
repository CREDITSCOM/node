#include <cmath>

#include <csdb/amount_commission.hpp>

#include "binary_streams.hpp"

namespace csdb {

AmountCommission::AmountCommission(uint16_t value) {
  u_.bits = value;
}

AmountCommission::AmountCommission(double value) {
  u_.fIEEE.sign = value < 0. ? 1 : 0;
  value = std::fabs(value);
  double expf = value == 0. ? 0. : std::log10(value);
  int expi = expf >= 0. ? expf + 0.5 : expf - 0.5;
  value /= std::pow(10, expi);
  if (value >= 1.) {
    value *= 0.1;
    ++expi;
  }
  u_.fIEEE.exp = expi + 18;
  u_.fIEEE.frac = lround(value * 1024);
}

void AmountCommission::put(priv::obstream& os) const {
  os.put(u_.bits);
}

bool AmountCommission::get(priv::ibstream& is) {
  return is.get(u_.bits);
}

}  // namespace csdb
