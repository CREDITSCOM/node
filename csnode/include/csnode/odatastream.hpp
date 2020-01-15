#ifndef ODATASTREAM_HPP
#define ODATASTREAM_HPP

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
/// The OData stream class represents an entity that fills data
///
template<typename T>
class ODataStream {
public:
    ///
    /// Constructor to write data
    ///
    explicit ODataStream(T& storage)
    : bytes_(&storage) {
    }

    ///
    /// Returns pointer to start of the data.
    ///
    char* data() const {
        return reinterpret_cast<char*>(bytes_->data());
    }

    ///
    /// Try to add field to stream.
    ///
    /// @param streamField Added type.
    ///
    template <typename V>
    inline void addValue(const V& streamField) {
        if (bytes_) {
            const char* ptr = reinterpret_cast<const char*>(&streamField);

            for (std::size_t i = 0; i < sizeof(V); ++i) {
                bytes_->push_back(static_cast<cs::Byte>(*(ptr + i)));
            }
        }
    }

    ///
    /// Adds char array to data stream.
    ///
    /// @param array Char array.
    ///
    template <typename V, std::size_t size>
    inline void addArray(const std::array<V, size>& array) {
        bytes_->insert(bytes_->end(), array.begin(), array.end());
    }

    ///
    /// Adds fixed string to stream.
    ///
    /// @param fixedString Template FixedString.
    ///
    template <std::size_t size>
    inline void addFixedString(const FixedString<size>& fixedString) {
        bytes_->insert(bytes_->end(), fixedString.begin(), fixedString.end());
    }

    ///
    /// Returns byte size of written data
    ///
    std::size_t size() const {
        return bytes_->size();
    }

    ///
    /// Returns stream empty status
    ///
    bool isEmpty() const {
        return size() == 0;
    }

    ///
    /// Adds bytes vector to stream.
    /// @param data Vector of bytes to write.
    ///
    void addVector(const cs::Bytes& data) {
        addValue(data.size());
        bytes_->insert(bytes_->end(), data.begin(), data.end());
    }

    ///
    /// Adds std::string chars to stream.
    ///
    /// @param string Any information represented as std::string.
    ///
    void addString(const std::string& string) {
        addValue(string.size());
        bytes_->insert(bytes_->end(), string.begin(), string.end());
    }

    ///
    /// @brief Adds bytesView entity to stream.
    ///
    void addBytesView(const cs::BytesView& bytesView) {
        addValue(bytesView.size());
        insertBytes(bytesView.data(), bytesView.size());
    }

    ///
    /// @brief Converts to template entity if compile.
    /// @brief For parsing converts last not readed part.
    ///
    template <typename V>
    V convert() const {
        return V(bytes_->begin(), bytes_->end());
    }

private:
    T* bytes_ = nullptr;

    void insertBytes(const char* data, std::size_t index) {
        bytes_->insert(bytes_->end(), data, data + index);
    }

    void insertBytes(const cs::Byte* data, std::size_t size) {
        insertBytes(reinterpret_cast<const char*>(data), size);
    }
};

template <typename T, typename V, std::size_t size>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const std::array<V, size>& array) {
    stream.addArray(array);
    return stream;
}

template <typename T, typename V>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const V& value) {
    static_assert(std::is_trivial_v<V>, "Template parameter to must be trivial. Overload this function for non-trivial type");
    stream.addValue(value);
    return stream;
}

template<typename T>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const cs::Bytes& data) {
    stream.addVector(data);
    return stream;
}

template<typename T>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const CompressedRegion& data) {
    stream << data.binarySize();
    stream << data.bytes();
    return stream;
}

template<typename T>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const cs::TransactionsPacketHash& hash) {
    stream << hash.toBinary();
    return stream;
}

template<typename T>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const std::string& data) {
    stream.addString(data);
    return stream;
}

template <typename T, std::size_t size>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const FixedString<size>& fixedString) {
    stream.addFixedString(fixedString);
    return stream;
}

template<typename T>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const cs::TransactionsPacket& packet) {
    stream << packet.toBinary();
    return stream;
}

template<typename T>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const csdb::PoolHash& hash) {
    stream << hash.to_binary();
    return stream;
}

template<typename T>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const csdb::Pool& pool) {
    uint32_t bSize;
    auto dataPtr = const_cast<csdb::Pool&>(pool).to_byte_stream(bSize);
    stream << cs::Bytes(dataPtr, dataPtr + bSize);
    return stream;
}

template <typename T, typename V, typename U>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const std::vector<V, U>& entities) {
    stream << entities.size();

    for (const auto& entity : entities) {
        stream << entity;
    }

    return stream;
}

template<typename T>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const cs::BytesView& bytesView) {
    stream.addBytesView(bytesView);
    return stream;
}

template <typename T, typename V, typename U>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const std::pair<V, U>& pair) {
    stream << pair.first;
    stream << pair.second;
    return stream;
}

template<typename T>
inline ODataStream<T>& operator<<(ODataStream<T>& stream, const csdb::Amount& amount) {
    stream << amount.toBytes();
    return stream;
}
}  // namespace cs

#endif  // ODATASTREAM_HPP
