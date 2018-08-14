#include "csdb/database.h"

#include <cstdio>
#include <cstdarg>
#include <map>

namespace {
struct last_error_struct {
  ::csdb::Database::Error last_error_ = ::csdb::Database::NoError;
  std::string last_error_message_;
};
last_error_struct& last_error_map(const ::csdb::Database* p)
{
  static thread_local ::std::map<const ::csdb::Database*, last_error_struct> last_errors_;
  return last_errors_[p];
}
} // namespace;

namespace csdb {
Database::Database()
{
  last_error_map(this) = last_error_struct();
}

Database::~Database()
{
}

Database::Iterator::Iterator()
{
}

Database::Iterator::~Iterator()
{
}

Database::Error Database::last_error() const
{
  return last_error_map(this).last_error_;
}

std::string Database::last_error_message() const
{
  const last_error_struct& les = last_error_map(this);
  if (!les.last_error_message_.empty()) {
    return les.last_error_message_;
  }
  switch (les.last_error_) {
  case NoError: return "No error";
  case NotFound: return "Database is not found";
  case Corruption: return "Database is corrupted";
  case NotSupported: return "Database is not supported";
  case InvalidArgument: return "Invalis argument passed";
  case IOError: return "I/O error";
  case NotOpen: return "Database is not open";
  default: return "Unknown error";
  }
}

void Database::set_last_error(Error error, const std::string& message)
{
  last_error_struct& les = last_error_map(this);
  les.last_error_ = error;
  les.last_error_message_ = message;
}

void Database::set_last_error(Error error, const char* message, ...)
{
  last_error_struct& les = last_error_map(this);
  les.last_error_ = error;
  if (nullptr != message) {
    va_list args1;
    va_start(args1, message);
    va_list args2;
    va_copy(args2, args1);
    les.last_error_message_.resize(std::vsnprintf(NULL, 0, message, args1) + 1);
    va_end(args1);
    std::vsnprintf(&(les.last_error_message_[0]), les.last_error_message_.size(), message, args2);
    va_end(args2);
    les.last_error_message_.resize(les.last_error_message_.size() - 1);
  } else {
    les.last_error_message_.clear();
  }
}

} // namespace csdb
