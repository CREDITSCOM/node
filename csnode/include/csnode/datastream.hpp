#ifndef DATASTREAM_HPP
#define DATASTREAM_HPP

#include <algorithm>
#include <csnode/nodecore.hpp>
#include <exception>
#include <string>
#include <type_traits>

#include <boost/asio/ip/udp.hpp>
#include <csdb/pool.hpp>

#include <lib/system/common.hpp>
#include <lib/system/structures.hpp>
#include <lib/system/utils.hpp>

namespace cs::csval {
const constexpr std::size_t v4Size = 4;
const constexpr std::size_t v6Size = 16;
}  // namespace cs::csval

namespace cs {
///
/// The Data stream class represents an entity that controls data from any char array.
///
class DataStream {
public:
    enum class Status {
        Read,
        Write
    };

    ///
    /// Constructors to read data from packet
    ///
    explicit DataStream(char* packet, std::size_t dataSize)
    : data_(packet)
    , index_(0)
    , dataSize_(dataSize) {
        head_ = data_;
    }

    explicit DataStream(const char* packet, std::size_t dataSize)
    : DataStream(const_cast<char*>(packet), dataSize) {
    }

    explicit DataStream(const uint8_t* packet, std::size_t dataSize)
    : DataStream(reinterpret_cast<const char*>(packet), dataSize) {
    }

    ///
    /// Constructor to write data
    ///
    explicit DataStream(cs::Bytes& storage)
    : bytes_(&storage) {
    }

    ///
    /// Try to get enpoint from data.
    ///
    /// @return Returns current end point from data.
    /// If data stream can not return valid enpoint then returns empty enpoint.
    ///
    boost::asio::ip::udp::endpoint parseEndpoint() {
        boost::asio::ip::udp::endpoint point;

        if (!isAvailable(sizeof(char))) {
            badState();
            return point;
        }

        char flags = *(data_ + index_);
        char v6 = flags & 1;
        char addressFlag = (flags >> 1) & 1;
        char portFlag = (flags >> 2) & 1;

        ++index_;

        std::size_t size = 0;

        if (addressFlag) {
            size += (v6) ? csval::v6Size : csval::v4Size;
        }

        if (portFlag) {
            size += sizeof(uint16_t);
        }

        if (!isAvailable(size)) {
            badState();
            return point;
        }

        boost::asio::ip::address address;

        if ((index_ + size) <= dataSize_) {
            if (addressFlag) {
                address = v6 ? boost::asio::ip::address(createAddress<boost::asio::ip::address_v6>()) : boost::asio::ip::address(createAddress<boost::asio::ip::address_v4>());
            }

            uint16_t port = 0;

            if (portFlag) {
                port = *(reinterpret_cast<uint16_t*>(data_ + index_));
                index_ += sizeof(uint16_t);
            }

            point = boost::asio::ip::udp::endpoint(address, port);
        }

        return point;
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
        if (!bytes_) {
            return data_ + index_;
        }
        else {
            return reinterpret_cast<char*>(bytes_->data());
        }
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
    /// Try to add field to stream.
    ///
    /// @param streamField Added type.
    ///
    template <typename T>
    inline void addValue(const T& streamField) {
        if (bytes_) {
            const char* ptr = reinterpret_cast<const char*>(&streamField);

            for (std::size_t i = 0; i < sizeof(T); ++i) {
                bytes_->push_back(static_cast<cs::Byte>(*(ptr + i)));
            }
        }
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
    /// Adds char array to data stream.
    ///
    /// @param array Char array.
    ///
    template <typename T, std::size_t size>
    inline void addArray(const std::array<T, size>& array) {
        if (bytes_) {
            bytes_->insert(bytes_->end(), array.begin(), array.end());
        }
    }

    ///
    /// Adds fixed string to stream.
    ///
    /// @param fixedString Template FixedString.
    ///
    template <std::size_t size>
    inline void addFixedString(const FixedString<size>& fixedString) {
        if (bytes_) {
            bytes_->insert(bytes_->end(), fixedString.begin(), fixedString.end());
        }
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
        if (!bytes_) {
            return dataSize_ - index_;
        }
        else {
            return bytes_->size();
        }
    }

    ///
    /// @brief Returns Read/Write status of stream.
    ///
    Status status() const {
        if (bytes_ != nullptr) {
            return Status::Write;
        }

        return Status::Read;
    }

    ///
    /// Adds enpoint to stream.
    ///
    /// @param enpoint Boost enpoint.
    ///
    void addEndpoint(const boost::asio::ip::udp::endpoint& endpoint) {
        if (!bytes_) {
            return;
        }

        char v6 = endpoint.address().is_v6();
        bytes_->push_back(static_cast<cs::Byte>((v6 | 6)));

        if (v6) {
            boost::asio::ip::address_v6::bytes_type bytes = endpoint.address().to_v6().to_bytes();
            addArray(bytes);
        }
        else {
            boost::asio::ip::address_v4::bytes_type bytes = endpoint.address().to_v4().to_bytes();
            addArray(bytes);
        }

        addValue(endpoint.port());
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
    /// Adds bytes vector to stream.
    /// @param data Vector of bytes to write.
    ///
    void addVector(const cs::Bytes& data) {
        if (bytes_) {
            addValue(data.size());
            bytes_->insert(bytes_->end(), data.begin(), data.end());
        }
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
    /// Adds std::string chars to stream.
    ///
    /// @param string Any information represented as std::string.
    ///
    void addString(const std::string& string) {
        if (bytes_) {
            addValue(string.size());
            bytes_->insert(bytes_->end(), string.begin(), string.end());
        }
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
    /// @brief Adds bytesView entity to stream.
    ///
    void addBytesView(const cs::BytesView& bytesView) {
        if (bytes_) {
            addValue(bytesView.size());
            insertBytes(bytesView.data(), bytesView.size());
        }
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
        if (bytes_) {
            return T(bytes_->begin(), bytes_->end());
        }
        else {
            return T(data_, data_ + (dataSize_ - index_));
        }
    }

private:
    // attributes
    char* data_ = nullptr;
    char* head_ = nullptr;

    std::size_t index_ = 0;
    std::size_t dataSize_ = 0;
    bool state_ = true;

    // main storage
    cs::Bytes* bytes_ = nullptr;

    // creates template address
    template <typename T>
    T createAddress() {
        typename T::bytes_type bytes;

        for (std::size_t i = 0; i < bytes.size(); ++i, ++index_) {
            bytes[i] = static_cast<unsigned char>(data_[index_]);
        }

        return T(bytes);
    }

    inline void badState() {
        state_ = false;
    }

    inline void insertBytes(const char* data, std::size_t index) {
        if (bytes_) {
            bytes_->insert(bytes_->end(), data, data + index);
        }
    }

    inline void insertBytes(const cs::Byte* data, std::size_t size) {
        insertBytes(reinterpret_cast<const char*>(data), size);
    }
};

///
/// Gets next end point from stream to end point variable.
///
inline DataStream& operator>>(DataStream& stream, boost::asio::ip::udp::endpoint& endPoint) {
    endPoint = stream.parseEndpoint();
    return stream;
}

///
/// Gets from stream to array.
///
template <typename T, std::size_t size>
inline DataStream& operator>>(DataStream& stream, std::array<T, size>& array) {
    array = stream.template parseArray<T, size>();
    return stream;
}

///
/// Gets from stream to T variable.
///
template <typename T>
inline DataStream& operator>>(DataStream& stream, T& value) {
    static_assert(std::is_trivial_v<T>, "Template parameter to must be trivial. Overload this function for non-trivial type");
    value = stream.template parseValue<T>();
    return stream;
}

///
/// Gets from stream to bytes vector (stream would use data size of vector to create bytes).
///
inline DataStream& operator>>(DataStream& stream, cs::Bytes& data) {
    data = stream.parseVector();
    return stream;
}

///
/// Gets from stream to std::string (stream would use data size of string to create bytes).
///
inline DataStream& operator>>(DataStream& stream, std::string& data) {
    data = stream.parseString();
    return stream;
}

///
/// Gets size of bytes from stream to fixedString.
///
template <std::size_t size>
inline DataStream& operator>>(DataStream& stream, FixedString<size>& fixedString) {
    fixedString = stream.template parseFixedString<size>();
    return stream;
}

///
/// Gets hashVector structure from stream.
///
inline DataStream& operator>>(DataStream& stream, cs::HashVector& hashVector) {
    stream >> hashVector.sender >> hashVector.hash >> hashVector.signature;
    return stream;
}

///
/// Gets hash matrix structure from stream.
///
inline DataStream& operator>>(DataStream& stream, cs::HashMatrix& matrix) {
    stream >> matrix.sender;

    for (std::size_t i = 0; i < kHashVectorCount; ++i) {
        stream >> matrix.hashVector[i];
    }

    stream >> matrix.signature;
    return stream;
}

///
/// Gets from stream to transactions packet hash.
///
inline DataStream& operator>>(DataStream& stream, cs::TransactionsPacketHash& hash) {
    cs::Bytes bytes;
    stream >> bytes;

    if (!bytes.empty()) {
        hash = cs::TransactionsPacketHash::fromBinary(bytes);
    }

    return stream;
}

///
/// Gets transaction packet structure from stream.
///
inline DataStream& operator>>(DataStream& stream, cs::TransactionsPacket& packet) {
    cs::Bytes bytes;
    stream >> bytes;

    if (!bytes.empty()) {
        packet = cs::TransactionsPacket::fromBinary(bytes);
    }

    return stream;
}

///
/// Gets transaction packet structure from stream.
///
inline DataStream& operator>>(DataStream& stream, csdb::PoolHash& hash) {
    cs::Bytes bytes;
    stream >> bytes;

    if (!bytes.empty()) {
        hash = csdb::PoolHash::from_binary(std::move(bytes));
    }

    return stream;
}

///
/// Gets pool from stream.
///
inline DataStream& operator>>(DataStream& stream, csdb::Pool& pool) {
    cs::Bytes bytes;
    stream >> bytes;

    if (!bytes.empty()) {
        pool = csdb::Pool::from_binary(std::move(bytes));
    }

    return stream;
}

///
/// Gets vector of entities from stream.
///
template <typename T, typename U>
inline DataStream& operator>>(DataStream& stream, std::vector<T, U>& entities) {
    std::size_t size;
    stream >> size;

    if (size == 0) {
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

///
/// Gets pool from stream.
///
inline DataStream& operator>>(DataStream& stream, cs::BytesView& bytesView) {
    bytesView = stream.parseBytesView();
    return stream;
}

///
/// Gets pair from stream.
///
template <typename T, typename U>
inline DataStream& operator>>(DataStream& stream, std::pair<T, U>& pair) {
    stream >> pair.first;
    stream >> pair.second;
    return stream;
}

///
/// Gets amount from stream.
///
template <typename T, typename U>
inline DataStream& operator>>(DataStream& stream, csdb::Amount& amount) {
    cs::Bytes bytes;
    stream >> bytes;

    if (stream.isValid()) {
        amount = csdb::Amount::fromBytes(bytes);
    }

    return stream;
}

///
/// Writes array to stream.
///
template <typename T, std::size_t size>
inline DataStream& operator<<(DataStream& stream, const std::array<T, size>& array) {
    stream.addArray(array);
    return stream;
}

///
/// Writes T to stream.
///
template <typename T>
inline DataStream& operator<<(DataStream& stream, const T& value) {
    static_assert(std::is_trivial_v<T>, "Template parameter to must be trivial. Overload this function for non-trivial type");
    stream.addValue(value);
    return stream;
}

///
/// Writes vector of bytes to stream.
///
inline DataStream& operator<<(DataStream& stream, const cs::Bytes& data) {
    stream.addVector(data);
    return stream;
}

///
/// Writes address to stream.
///
inline DataStream& operator<<(DataStream& stream, const boost::asio::ip::udp::endpoint& endpoint) {
    stream.addEndpoint(endpoint);
    return stream;
}

///
/// Writes hash binary to stream.
///
inline DataStream& operator<<(DataStream& stream, const cs::TransactionsPacketHash& hash) {
    stream << hash.toBinary();
    return stream;
}

///
/// Writes std::string to stream.
///
inline DataStream& operator<<(DataStream& stream, const std::string& data) {
    stream.addString(data);
    return stream;
}

///
/// Writes fixed string to stream.
///
template <std::size_t size>
inline DataStream& operator<<(DataStream& stream, const FixedString<size>& fixedString) {
    stream.addFixedString(fixedString);
    return stream;
}

///
/// Writes hash vector structure to stream.
///
inline DataStream& operator<<(DataStream& stream, const cs::HashVector& hashVector) {
    stream << hashVector.sender;
    stream << hashVector.hash;
    stream << hashVector.signature;
    return stream;
}

///
/// Writes hash matrix structure to stream.
///
inline DataStream& operator<<(DataStream& stream, const cs::HashMatrix& matrix) {
    stream << matrix.sender;

    for (std::size_t i = 0; i < kHashVectorCount; ++i) {
        stream << matrix.hashVector[i];
    }

    stream << matrix.signature;
    return stream;
}

///
/// Writes hash matrix structure to stream.
///
inline DataStream& operator<<(DataStream& stream, const cs::TransactionsPacket& packet) {
    stream << packet.toBinary();
    return stream;
}

///
/// Writes hash matrix structure to stream.
///
inline DataStream& operator<<(DataStream& stream, const csdb::PoolHash& hash) {
    stream << hash.to_binary();
    return stream;
}

///
/// Writes pool structure to stream as byte representation.
///
inline DataStream& operator<<(DataStream& stream, const csdb::Pool& pool) {
    uint32_t bSize;
    auto dataPtr = const_cast<csdb::Pool&>(pool).to_byte_stream(bSize);
    stream << cs::Bytes(dataPtr, dataPtr + bSize);
    return stream;
}

///
/// Writes vector of entities to stream.
///
template <typename T, typename U>
inline DataStream& operator<<(DataStream& stream, const std::vector<T, U>& entities) {
    stream << entities.size();

    for (const auto& entity : entities) {
        stream << entity;
    }

    return stream;
}

///
/// Writes bytes view to stream.
///
inline DataStream& operator<<(DataStream& stream, const cs::BytesView& bytesView) {
    stream.addBytesView(bytesView);
    return stream;
}

///
/// Writes pair to stream.
///
template <typename T, typename U>
inline DataStream& operator<<(DataStream& stream, const std::pair<T, U>& pair) {
    stream << pair.first;
    stream << pair.second;
    return stream;
}

///
/// Writes csdb Amount to stream
///
inline DataStream& operator<<(DataStream& stream, const csdb::Amount& amount) {
    stream << amount.toBytes();
    return stream;
}
}  // namespace cs

#endif  // DATASTREAM_HPP
