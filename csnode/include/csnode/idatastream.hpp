#ifndef IDATASTREAM_HPP
#define IDATASTREAM_HPP

#include <algorithm>
#include <exception>
#include <string>
#include <type_traits>

#include <csnode/nodecore.hpp>

#include <csdb/pool.hpp>

#include <lib/system/common.hpp>
#include <lib/system/structures.hpp>
#include <lib/system/utils.hpp>

namespace cs {
///
/// The IData stream class represents an entity that reads data from any char array.
///
class IDataStream {
public:
    ///
    /// Constructors to read data from packet
    ///
    explicit IDataStream(char* packet, std::size_t dataSize)
    : data_(packet)
    , index_(0)
    , dataSize_(dataSize) {
        head_ = data_;
    }

    explicit IDataStream(const char* packet, std::size_t dataSize)
    : IDataStream(const_cast<char*>(packet), dataSize) {
    }

    explicit IDataStream(const uint8_t* packet, std::size_t dataSize)
    : IDataStream(reinterpret_cast<const char*>(packet), dataSize) {
    }

    ///
    /// Returns current state of stream.
    ///
    /// @return Returns state of stream.
    ///
    bool isValid() const {
        if (index_ > dataSize_) {
            return false;
        }

        return state_;
    }

    ///
    /// Returns state of available bytes.
    ///
    /// @param size Count of bytes.
    /// @return Returns state of available bytes.
    ///
    bool isAvailable(std::size_t size) {
        return (index_ + size) <= dataSize_;
    }

    ///
    /// Returns pointer to start of the data.
    ///
    char* data() const {
        return data_ + index_;
    }

    ///
    /// Try to get field from stream by sizeof(T).
    ///
    /// @return Returns stream field.
    /// If stream can not return field than returns empty T().
    ///
    template <typename T>
    inline T parseValue() {
        if (!isAvailable(sizeof(T))) {
            badState();
            return T();
        }

        T field = cs::Utils::getFromArray<T>(data_, index_);
        index_ += sizeof(T);

        return field;
    }

    ///
    /// Returns byte array from stream.
    ///
    /// @return Returns byte array.
    /// If stream can not return valid array than returns empty byte array.
    ///
    template <typename T, std::size_t size>
    inline std::array<T, size> parseArray() {
        std::array<T, size> array = {0};

        if (!isAvailable(size)) {
            badState();
            return array;
        }

        for (std::size_t i = 0; i < size; ++i) {
            array[i] = static_cast<T>(data_[i + index_]);
        }

        index_ += size;
        return array;
    }

    ///
    /// Returns fixed string by template size.
    ///
    /// @return Returns template FixedString.
    /// If stream can not return available bytes size it returns zero FixedString.
    ///
    template <std::size_t size>
    inline FixedString<size> parseFixedString() {
        FixedString<size> str;

        if (!isAvailable(size)) {
            badState();
            return str;
        }

        for (std::size_t i = 0; i < size; ++i) {
            str[i] = data_[i + index_];
        }

        index_ += size;

        return str;
    }

    ///
    /// Returns byte size of write/read data.
    ///
    /// @return Returns data stream size.
    ///
    std::size_t size() const {
        return dataSize_ - index_;
    }

    ///
    /// Returns stream empty status
    ///
    bool isEmpty() const {
        return size() == 0;
    }

    ///
    /// Skips compile time size.
    ///
    /// If stream can not skip size than it does nothing.
    ///
    template <std::size_t size>
    inline void skip() {
        if (!isAvailable(size)) {
            badState();
            return;
        }

        index_ += size;
    }

    ///
    /// Skips run-time size.
    ///
    void skip(size_t size) {
        if (!isAvailable(size)) {
            badState();
            return;
        }

        index_ += size;
    }

    ///
    /// Returns bytes vector.
    ///
    /// @return Returns byte vector.
    /// If stream can not return size of bytes it returns empty vector.
    ///
    cs::Bytes parseVector() {
        cs::Bytes result;

        if (!isAvailable(sizeof(std::size_t))) {
            badState();
            return result;
        }

        std::size_t size = parseValue<size_t>();

        if (isAvailable(size)) {
            result = cs::Bytes(data_ + index_, data_ + index_ + size);
            index_ += size;
        }
        else {
            badState();
        }

        return result;
    }

    ///
    /// @return Returns std::string by arguments size.
    /// If stream can not return size of bytes it returns empty std::string.
    ///
    std::string parseString() {
        std::string result;

        if (!isAvailable(sizeof(std::size_t))) {
            badState();
            return result;
        }

        std::size_t size = parseValue<size_t>();

        if (isAvailable(size)) {
            result = std::string(data_ + index_, data_ + index_ + size);
            index_ += size;
        }
        else {
            badState();
        }

        return result;
    }

    ///
    /// @brief Returns BytesView entity if can, otherwise return empty object.
    ///
    cs::BytesView parseBytesView() {
        cs::BytesView bytesView;
        size_t size = parseValue<size_t>();

        if (isAvailable(size)) {
            bytesView = cs::BytesView(reinterpret_cast<cs::Byte*>(data_), size);
            index_ += size;
        }
        else {
            badState();
        }

        return bytesView;
    }

    ///
    /// Peeks next parameter.
    ///
    /// @return Returns next T parameter.
    ///
    template <typename T>
    inline const T& peek() const {
        return *(reinterpret_cast<T*>(data_ + index_));
    }

    ///
    /// @brief Converts to template entity if compile.
    /// @brief For parsing converts last not readed part.
    ///
    template <typename T>
    T convert() const {
        return T(data_, data_ + (dataSize_ - index_));
    }

private:
    char* data_ = nullptr;
    char* head_ = nullptr;

    std::size_t index_ = 0;
    std::size_t dataSize_ = 0;
    bool state_ = true;

    void badState() {
        state_ = false;
    }
};

template <typename T, std::size_t size>
inline IDataStream& operator>>(IDataStream& stream, std::array<T, size>& array) {
    array = stream.template parseArray<T, size>();
    return stream;
}

template <typename T>
inline IDataStream& operator>>(IDataStream& stream, T& value) {
    static_assert(std::is_trivial_v<T>, "Template parameter to must be trivial. Overload this function for non-trivial type");
    value = stream.template parseValue<T>();
    return stream;
}

inline IDataStream& operator>>(IDataStream& stream, cs::Bytes& data) {
    data = stream.parseVector();
    return stream;
}

inline IDataStream& operator>>(IDataStream& stream, std::string& data) {
    data = stream.parseString();
    return stream;
}

template <std::size_t size>
inline IDataStream& operator>>(IDataStream& stream, FixedString<size>& fixedString) {
    fixedString = stream.template parseFixedString<size>();
    return stream;
}

inline IDataStream& operator>>(IDataStream& stream, cs::TransactionsPacketHash& hash) {
    cs::Bytes bytes;
    stream >> bytes;

    if (!bytes.empty()) {
        hash = cs::TransactionsPacketHash::fromBinary(bytes);
    }

    return stream;
}

inline IDataStream& operator>>(IDataStream& stream, cs::TransactionsPacket& packet) {
    cs::Bytes bytes;
    stream >> bytes;

    if (!bytes.empty()) {
        packet = cs::TransactionsPacket::fromBinary(bytes);
    }

    return stream;
}

inline IDataStream& operator>>(IDataStream& stream, csdb::PoolHash& hash) {
    cs::Bytes bytes;
    stream >> bytes;

    if (!bytes.empty()) {
        hash = csdb::PoolHash::from_binary(std::move(bytes));
    }

    return stream;
}

inline IDataStream& operator>>(IDataStream& stream, csdb::Pool& pool) {
    cs::Bytes bytes;
    stream >> bytes;

    if (!bytes.empty()) {
        pool = csdb::Pool::from_binary(std::move(bytes));
    }

    return stream;
}

template <typename T, typename U>
inline IDataStream& operator>>(IDataStream& stream, std::vector<T, U>& entities) {
    std::size_t size = 0;
    stream >> size;

    if (size == 0) {
        return stream;
    }

    if (!stream.isAvailable(size)) {
        return stream;
    }

    std::vector<T, U> expectedEntities;
    expectedEntities.reserve(size);

    for (std::size_t i = 0; i < size; i++) {
        T entity;
        stream >> entity;

        expectedEntities.push_back(entity);
    }

    if (size != expectedEntities.size()) {
        cserror() << "Data stream parsing of vector: vector parsing failed";
    }

    entities = std::move(expectedEntities);
    return stream;
}

inline IDataStream& operator>>(IDataStream& stream, cs::BytesView& bytesView) {
    bytesView = stream.parseBytesView();
    return stream;
}

template <typename T, typename U>
inline IDataStream& operator>>(IDataStream& stream, std::pair<T, U>& pair) {
    stream >> pair.first;
    stream >> pair.second;
    return stream;
}

inline IDataStream& operator>>(IDataStream& stream, csdb::Amount& amount) {
    cs::Bytes bytes;
    stream >> bytes;

    if (stream.isValid()) {
        amount = csdb::Amount::fromBytes(bytes);
    }

    return stream;
}

inline IDataStream& operator>>(IDataStream& stream, CompressedRegion& region) {
    std::size_t binarySize = 0;
    stream >> binarySize;

    cs::Bytes bytes;
    stream >> bytes;

    if (!bytes.empty()) {
        region = CompressedRegion { std::move(bytes), binarySize };
    }

    return stream;
}
}  // namespace cs

#endif  // IDATASTREAM_HPP
