#include "csdb/currency.h"
#include "csdb/internal/shared_data_ptr_implementation.h"
#include "binary_streams.h"

namespace csdb {

class Currency::priv : public ::csdb::internal::shared_data
{
public:
  std::string name;
};
SHARED_DATA_CLASS_IMPLEMENTATION(Currency)

Currency::Currency(const std::string &name) :
  Currency()
{
  d->name = name;
}

bool Currency::is_valid() const noexcept
{
  return !d->name.empty();
}

std::string Currency::to_string() const noexcept
{
  return d->name;
}

bool Currency::operator ==(const Currency &other) const noexcept
{
  return d->name == other.d->name;
}

bool Currency::operator !=(const Currency &other) const noexcept
{
  return !operator==(other);
}

bool Currency::operator <(const Currency &other) const noexcept
{
  return d->name < other.d->name;
}

void Currency::put(::csdb::priv::obstream &os) const
{
  os.put(d->name);
}

bool Currency::get(::csdb::priv::ibstream &is)
{
  return is.get(d->name);
}

} // namespace csdb
