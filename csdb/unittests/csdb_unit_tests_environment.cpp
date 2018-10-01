#include "csdb_unit_tests_environment.h"

namespace csdb {

bool operator ==(const Transaction& a, const Transaction& b)
{
  if (!((a.source() == b.source())
        && (a.target() == b.target())
        && (a.currency() == b.currency())
        && (a.amount() == b.amount())
        && (a.balance() == b.balance()))) {
    return false;
  }

  auto ids = a.user_field_ids();
  if (ids != b.user_field_ids()) {
    return false;
  }

  for (auto id : ids) {
    if (a.user_field(id) != b.user_field(id)) {
      return false;
    }
  }

  return true;
}

bool operator ==(const Pool& a, const Pool& b)
{
  if (a.is_valid() != b.is_valid()) {
    return false;
  }

  if (!a.is_valid()) {
    return true;
  }

  if ((a.is_read_only() != b.is_read_only())
      || (a.sequence() != b.sequence())
      || (a.previous_hash() != b.previous_hash())
      || (a.transactions_count() != b.transactions_count())
      ) {
    return false;
  }

  if (a.is_read_only()) {
    if (a.hash() != b.hash()) {
      return false;
    }
  }

  for (size_t i = 0; i < a.transactions_count(); ++i) {
    if (a.transaction(i) != b.transaction(i)) {
      return false;
    }
  }

  auto ids = a.user_field_ids();
  if (ids != b.user_field_ids()) {
    return false;
  }

  for (auto id : ids) {
    if (a.user_field(id) != b.user_field(id)) {
      return false;
    }
  }

  return true;
}

} // namespace csdb

::std::ostream& operator <<(::std::ostream& os, const ::csdb::UserField& value)
{
  os << "{";
  switch (value.type()) {
  case ::csdb::UserField::Integer:
    os << "Integer: " << value.value<int>();
    break;

  case ::csdb::UserField::String:
    os << "String: \"" << value.value<::std::string>() << "\"";
    break;

  case ::csdb::UserField::Amount:
    os << "Amount: " << value.value<::csdb::Amount>();
    break;

  default:
    os << "<invalid>";
    break;
  }
  os << "}";
  return os;
}
