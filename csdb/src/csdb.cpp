#include "csdb/csdb.h"

namespace csdb {

namespace {
::csdb::Storage instance;
} // namespace;

bool init(const Storage::OpenOptions& options, Storage::OpenCallback callback)
{
  if (instance.isOpen()) {
    return false;
  }
  instance = ::csdb::Storage::get(options, callback);
  return instance.isOpen();
}

bool init(const char* path_to_bases, Storage::OpenCallback callback)
{
  if (instance.isOpen()) {
    return false;
  }
  instance = ::csdb::Storage::get(path_to_bases, callback);
  return instance.isOpen();
}

bool isInitialized()
{
  return instance.isOpen();
}

void done()
{
  instance.close();
}

Storage::Error lastError()
{
  return instance.last_error();
}

::std::string lastErrorMessage()
{
  return instance.last_error_message();
}

Database::Error dbLastError()
{
  return instance.db_last_error();
}

::std::string dbLastErrorMessage()
{
  return instance.last_error_message();
}

Storage defaultStorage()
{
  return instance;
}

}
