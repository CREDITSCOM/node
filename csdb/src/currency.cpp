#include "csdb/currency.hpp"
#include "binary_streams.hpp"
#include "csdb/internal/shared_data_ptr_implementation.hpp"

namespace csdb {

class Currency::priv : public ::csdb::internal::shared_data {
public:
  uint8_t id = 0;
};
SHARED_DATA_CLASS_IMPLEMENTATION(Currency)

Currency::Currency(const uint8_t &id)
: Currency() {
  d->id = id;
}

bool Currency::is_valid() const noexcept {
  return d != nullptr;
}

std::string Currency::to_string() const noexcept {
  return std::to_string(d->id);
}

bool Currency::operator==(const Currency &other) const noexcept {
  return d->id == other.d->id;
}

bool Currency::operator!=(const Currency &other) const noexcept {
  return !operator==(other);
}

bool Currency::operator<(const Currency &other) const noexcept {
  return d->id < other.d->id;
}

void Currency::put(::csdb::priv::obstream &os) const {
  os.put(d->id);
}

bool Currency::get(::csdb::priv::ibstream &is) {
  return is.get(d->id);
}

}  // namespace csdb
