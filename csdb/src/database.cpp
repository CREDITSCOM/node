#include "csdb/database.h"

#include <cstdio>
#include <cstdarg>

namespace csdb {
Database::Database() :
  last_error_(NoError)
{
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

std::string Database::last_error_message() const
{
  if (!last_error_message_.empty()) {
    return last_error_message_;
  }
  switch (last_error_) {
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
  last_error_ = error;
  last_error_message_ = message;
}

void Database::set_last_error(Error error, const char* message, ...)
{
  last_error_ = error;
  if (nullptr != message) {
    va_list args1;
    va_start(args1, message);
    va_list args2;
    va_copy(args2, args1);
    last_error_message_.resize(std::vsnprintf(NULL, 0, message, args1) + 1);
    va_end(args1);
    std::vsnprintf(&(last_error_message_[0]), last_error_message_.size(), message, args2);
    va_end(args2);
    last_error_message_.resize(last_error_message_.size() - 1);
  } else {
    last_error_message_.clear();
  }
}

} // namespace csdb
