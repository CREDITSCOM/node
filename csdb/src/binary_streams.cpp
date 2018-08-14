#include "binary_streams.h"
#include "integral_encdec.h"
#include <cstring>

namespace csdb {
namespace priv {

void obstream::put(const void *buf, size_t size)
{
  const uint8_t *data = reinterpret_cast<const uint8_t*>(buf);
  buffer_.insert(buffer_.end(), data, data + size);
}

void obstream::put(const std::string &value)
{
  put(static_cast<uint32_t>(value.size()));
  buffer_.insert(buffer_.end(), value.begin(), value.end());
}

void obstream::put(const internal::byte_array &value)
{
  put(value.size());
  buffer_.insert(buffer_.end(), value.begin(), value.end());
}

bool ibstream::get(void *buf, size_t size)
{
  if (size > size_) {
    return false;
  }

  const uint8_t *src = static_cast<const uint8_t*>(data_);
  std::memmove(buf, src, size);
  size_ -= size;
  data_ = static_cast<const void*>(src + size);
  return true;
}

bool ibstream::get(std::string &value)
{
  uint32_t size;
  if (!get(size)) {
    return false;
  }
  if (size > size_) {
    return false;
  }

  const char *data = static_cast<const char*>(data_);
  value.assign(data, size);
  size_ -= size;
  data_ = static_cast<const void*>(data + size);
  return true;
}

bool ibstream::get(internal::byte_array &value)
{
  size_t size;
  if (!get(size)) {
    return false;
  }
  if (size > size_) {
    return false;
  }

  const uint8_t *data = static_cast<const uint8_t*>(data_);
  value.assign(data, data + size);
  size_ -= size;
  data_ = static_cast<const void*>(data + size);
  return true;
}

} // namespace priv
} // namespace csdb
