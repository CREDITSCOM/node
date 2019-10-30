#include "../include/csnode/dynamicbuffer.hpp"

#include <algorithm>

cs::DynamicBuffer::DynamicBuffer(size_t size)
: size_(size) {
    array_ = new char[size];
}

cs::DynamicBuffer::DynamicBuffer(const char* data, std::size_t size)
: size_(size) {
    array_ = new char[size];

    std::copy(data, data + size, array_);
}

cs::DynamicBuffer::DynamicBuffer(const unsigned char* data, std::size_t size)
: DynamicBuffer(reinterpret_cast<const char*>(data), size) {
}

cs::DynamicBuffer::DynamicBuffer(const cs::DynamicBuffer& buffer)
: size_(buffer.size_) {
    array_ = new char[size_];

    std::copy(buffer.array_, buffer.array_ + buffer.size_, array_);
}

cs::DynamicBuffer::DynamicBuffer(cs::DynamicBuffer&& buffer)
: array_(buffer.array_)
, size_(buffer.size_) {
    buffer.array_ = nullptr;
    buffer.size_ = 0;
}

cs::DynamicBuffer& cs::DynamicBuffer::operator=(const cs::DynamicBuffer& buffer) {
    if (this != &buffer) {
        if (array_) {
            delete[] array_;
        }

        size_ = buffer.size_;
        array_ = new char[size_];

        std::copy(buffer.array_, buffer.array_ + buffer.size_, array_);
    }

    return *this;
}

cs::DynamicBuffer& cs::DynamicBuffer::operator=(cs::DynamicBuffer&& buffer) {
    if (this != &buffer) {
        if (array_) {
            delete[] array_;
        }

        size_ = buffer.size_;
        array_ = buffer.array_;

        buffer.size_ = 0;
        buffer.array_ = nullptr;
    }

    return *this;
}

cs::DynamicBuffer::~DynamicBuffer() {
    if (array_) {
        delete[] array_;

        array_ = nullptr;
        size_ = 0;
    }
}

char& cs::DynamicBuffer::operator[](std::size_t index) {
    return const_cast<char&>(static_cast<const DynamicBuffer*>(this)->operator[](index));
}

const char& cs::DynamicBuffer::operator[](std::size_t index) const {
    return *(array_ + index);
}

char* cs::DynamicBuffer::get() const {
    return array_;
}

char* cs::DynamicBuffer::operator*() const {
    return this->get();
}

size_t cs::DynamicBuffer::size() const {
    return size_;
}

char* cs::DynamicBuffer::begin() {
    return array_;
}

char* cs::DynamicBuffer::end() {
    return array_ + size_;
}

const char* cs::DynamicBuffer::begin() const {
    return array_;
}

const char* cs::DynamicBuffer::end() const {
    return array_ + size_;
}

bool cs::operator==(const cs::DynamicBuffer& lhs, const cs::DynamicBuffer& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i] != rhs[i]) {
            return false;
        }
    }

    return true;
}

bool cs::operator!=(const cs::DynamicBuffer& lhs, const cs::DynamicBuffer& rhs) {
    return !(lhs == rhs);
}

void cs::swap(cs::DynamicBuffer& lhs, cs::DynamicBuffer& rhs) {
    if (&lhs != &rhs) {
        std::swap(lhs.size_, rhs.size_);
        std::swap(lhs.array_, rhs.array_);
    }
}
