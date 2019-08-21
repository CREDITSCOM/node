#include <cstring>

#include "binary_streams.hpp"
#include "integral_encdec.hpp"

namespace csdb {
namespace priv {

void obstream::put(const void *buf, size_t size) {
    auto data = static_cast<const uint8_t *>(buf);
    buffer_.insert(buffer_.end(), data, data + size);
}

void obstream::put(const std::string &value) {
    put(static_cast<uint32_t>(value.size()));
    buffer_.insert(buffer_.end(), value.begin(), value.end());
}

void obstream::put(const cs::Bytes &value) {
    put(value.size());
    buffer_.insert(buffer_.end(), value.begin(), value.end());
}

bool ibstream::get(void *buf, size_t size) {
    if (size > size_) {
        return false;
    }

    auto src = static_cast<const uint8_t *>(data_);
    std::memmove(buf, src, size);
    size_ -= size;
    data_ = static_cast<const void *>(src + size);
    return true;
}

bool ibstream::get(std::string &value) {
    uint32_t size;
    if (!get(size)) {
        return false;
    }
    if (size > size_) {
        return false;
    }

    auto data = static_cast<const char *>(data_);
    value.assign(data, size);
    size_ -= size;
    data_ = static_cast<const void *>(data + size);
    return true;
}

bool ibstream::get(cs::Bytes &value) {
    size_t size;
    if (!get(size)) {
        return false;
    }
    if (size > size_) {
        return false;
    }

    const auto data = static_cast<const uint8_t *>(data_);
    value.assign(data, data + size);
    size_ -= size;
    data_ = static_cast<const void *>(data + size);
    return true;
}

}  // namespace priv
}  // namespace csdb
