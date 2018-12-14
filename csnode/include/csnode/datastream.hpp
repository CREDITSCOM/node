#ifndef DATASTREAM_HPP
#define DATASTREAM_HPP

#include <algorithm>
#include <csnode/nodecore.hpp>
#include <exception>
#include <string>
#include <type_traits>

#include <csdb/pool.hpp>
#include <boost/asio/ip/udp.hpp>

#include <lib/system/common.hpp>
#include <lib/system/structures.hpp>
#include <lib/system/utils.hpp>

namespace cs {
///
/// Exception for packet stream.
///
class DataStreamException : public std::exception {
public:
  explicit DataStreamException(const std::string& message);
  virtual const char* what() const noexcept override;

private:
  const std::string message_;
};

///
/// The Data stream class represents an entity that controls data from any char array.
///
class DataStream {
public:
  ///
  /// Constructors to read data from packet
  ///
  explicit DataStream(char* packet, std::size_t dataSize);
  explicit DataStream(const char* packet, std::size_t dataSize);
  explicit DataStream(const uint8_t* packet, std::size_t dataSize);

  ///
  /// Constructor to write data
  ///
  explicit DataStream(cs::Bytes& storage);

  ///
  /// Try to get enpoint from data.
  ///
  /// @return Returns current end point from data.
  /// If data stream can not return valid enpoint then returns empty enpoint.
  ///
  boost::asio::ip::udp::endpoint endpoint();

  ///
  /// Returns current state of stream.
  ///
  /// @return Returns state of stream.
  ///
  bool isValid() const;

  ///
  /// Returns state of available bytes.
  ///
  /// @param size Count of bytes.
  /// @return Returns state of available bytes.
  ///
  bool isAvailable(std::size_t size);

  ///
  /// Returns pointer to start of the data.
  ///
  char* data() const;

  ///
  /// Try to get field from stream by sizeof(T).
  ///
  /// @return Returns stream field.
  /// If stream can not return field than returns empty T().
  ///
  template <typename T>
  inline T streamField() {
    if (!isAvailable(sizeof(T))) {
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
  inline void setStreamField(const T& streamField) {
    if (bytes_) {
      const char* ptr = reinterpret_cast<const char*>(&streamField);

      for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes_->push_back(*(ptr + i));
      }
    }
  }

  ///
  /// Returns char array from stream.
  ///
  /// @return Returns char array.
  /// If stream can not return valid array than returns empty char array.
  ///
  template <std::size_t size>
  inline std::array<char, size> streamArray() {
    std::array<char, size> array = {0};

    if (!isAvailable(size)) {
      return array;
    }

    for (std::size_t i = 0; i < size; ++i) {
      array[i] = data_[i + index_];
    }

    index_ += size;

    return array;
  }

  ///
  /// Adds char array to data stream.
  ///
  /// @param array Char array.
  ///
  template <std::size_t size>
  inline void setStreamArray(const std::array<char, size>& array) {
    if (bytes_) {
      bytes_->insert(bytes_->end(), array.begin(), array.end());
    }
  }

  ///
  /// Adds array to stream.
  ///
  /// @param array Byte array.
  ///
  template <std::size_t size>
  inline void setByteArray(const ByteArray<size>& array) {
    if (bytes_) {
      bytes_->insert(bytes_->end(), array.begin(), array.end());
    }
  }

  ///
  /// Returns static byte array.
  ///
  /// @return Returns byte array.
  /// If stream can not returns valid byte array it returns empty array.
  ///
  template <std::size_t size>
  inline ByteArray<size> byteArray() {
    ByteArray<size> result = {0};

    if (!isAvailable(size)) {
      return result;
    }

    for (std::size_t i = 0; i < size; ++i) {
      result[i] = static_cast<unsigned char>(data_[i + index_]);
    }

    index_ += size;
    return result;
  }

  ///
  /// Adds fixed string to stream.
  ///
  /// @param fixedString Template FixedString.
  ///
  template <std::size_t size>
  inline void setFixedString(const FixedString<size>& fixedString) {
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
  inline FixedString<size> fixedString() {
    FixedString<size> str;

    if (!isAvailable(size)) {
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
  std::size_t size() const;

  ///
  /// Adds enpoint to stream.
  ///
  /// @param enpoint Boost enpoint.
  ///
  void addEndpoint(const boost::asio::ip::udp::endpoint& endpoint);

  ///
  /// Skips compile time size.
  ///
  /// If stream can not skip size than it does nothing.
  ///
  template <std::size_t size>
  inline void skip() {
    if (!isAvailable(size)) {
      return;
    }

    index_ += size;
  }

  ///
  /// Adds transactions packet hash to stream.
  ///
  /// @param hash Data base transactions packet hash.
  ///
  void addTransactionsHash(const cs::TransactionsPacketHash& hash);

  ///
  /// Returns packet hash.
  ///
  /// @return Returns packet hash.
  /// If stream can not return hash it returns empty hash.
  ///
  cs::TransactionsPacketHash transactionsHash();

  ///
  /// Adds bytes vector to stream.
  /// @param data Vector of bytes to write.
  ///
  void addVector(const cs::Bytes& data);

  ///
  /// Returns bytes vector.
  ///
  /// @return Returns byte vector.
  /// If stream can not return size of bytes it returns empty vector.
  ///
  cs::Bytes byteVector();

  ///
  /// Adds std::string chars to stream.
  ///
  /// @param string Any information represented as std::string.
  ///
  void addString(const std::string& string);

  ///
  /// Returns std::string from stream.
  ///        /// @return Returns std::string by arguments size.
  /// If stream can not return size of bytes it returns empty std::string.
  ///
  std::string string();

  ///
  /// Adds hash vector to stream.
  ///
  /// @param hashVector HashVector structure.
  ///
  void addHashVector(const cs::HashVector& hashVector);

  ///
  /// Returns parsed hash vector structure.
  ///
  cs::HashVector hashVector();

  ///
  /// Adds hash matrix structure to stream.
  ///
  /// @param matrix Hash matrix that should be added to stream.
  ///
  void addHashMatrix(const cs::HashMatrix& matrix);

  ///
  /// Returns parsed hash matrix.
  ///
  /// @return Initialized HashMatrix structure.
  ///
  cs::HashMatrix hashMatrix();

  ///
  /// Adds transaction packet to stream.
  ///
  /// @param packet Packet that should be added to stream.
  ///
  void addTransactionsPacket(const cs::TransactionsPacket& packet);

  ///
  /// Returns parsed transaction packet from stream.
  ///
  /// @return Initialized and parsed transaction packet
  ///
  cs::TransactionsPacket transactionPacket();

  ///
  /// Peeks next parameter.
  ///
  /// @return Returns next T parameter.
  ///
  template <typename T>
  inline const T& peek() const {
    return *(reinterpret_cast<T*>(data_ + index_));
  }

private:
  // attributes
  char* data_ = nullptr;
  char* head_ = nullptr;

  std::size_t index_ = 0;
  std::size_t dataSize_ = 0;

  cs::Bytes* bytes_ = nullptr;

  // creates template address
  template <typename T>
  T createAddress();

  template <typename T>
  inline void insertToArray(char* data, std::size_t index, T value) {
    char* ptr = reinterpret_cast<char*>(&value);

    for (std::size_t i = index, k = 0; i < index + sizeof(T); ++i, ++k) {
      *(data + i) = *(ptr + k);
    }
  }
};

///
/// Gets next end point from stream to end point variable.
///
inline DataStream& operator>>(DataStream& stream, boost::asio::ip::udp::endpoint& endPoint) {
  endPoint = stream.endpoint();
  return stream;
}

///
/// Gets from stream to uint8_t variable.
///
template <typename T>
inline DataStream& operator>>(DataStream& stream, T& streamField) {
  static_assert(std::is_trivial<T>::value, "Template parameter to must be trivial. Overload this function for non-trivial type");
  streamField = stream.streamField<T>();
  return stream;
}

///
/// Gets from stream to array.
///
template <std::size_t size>
inline DataStream& operator>>(DataStream& stream, std::array<char, size>& array) {
  array = stream.streamArray<size>();
  return stream;
}

///
/// Gets from stream to byte array.
///
template <std::size_t size>
inline DataStream& operator>>(DataStream& stream, ByteArray<size>& array) {
  array = stream.byteArray<size>();
  return stream;
}

///
/// Gets from stream to transactions packet hash.
///
inline DataStream& operator>>(DataStream& stream, cs::TransactionsPacketHash& hash) {
  hash = stream.transactionsHash();
  return stream;
}

///
/// Gets from stream to bytes vector (stream would use data size of vector to create bytes).
///
inline DataStream& operator>>(DataStream& stream, cs::Bytes& data) {
  data = stream.byteVector();
  return stream;
}

///
/// Gets from stream to std::string (stream would use data size of string to create bytes).
///
inline DataStream& operator>>(DataStream& stream, std::string& data) {
  data = stream.string();
  return stream;
}

///
/// Gets size of bytes from stream to fixedString.
///
template <std::size_t size>
inline DataStream& operator>>(DataStream& stream, FixedString<size>& fixedString) {
  fixedString = stream.fixedString<size>();
  return stream;
}

///
/// Gets hashVector structure from stream.
///
inline DataStream& operator>>(DataStream& stream, cs::HashVector& hashVector) {
  hashVector = stream.hashVector();
  return stream;
}

///
/// Gets hash matrix structure from stream.
///
inline DataStream& operator>>(DataStream& stream, cs::HashMatrix& hashMatrix) {
  hashMatrix = stream.hashMatrix();
  return stream;
}

///
/// Gets transaction packet structure from stream.
///
inline DataStream& operator>>(DataStream& stream, cs::TransactionsPacket& packet) {
  packet = stream.transactionPacket();
  return stream;
}

///
/// Gets transaction packet structure from stream.
///
inline DataStream& operator>>(DataStream& stream, csdb::PoolHash& hash) {
  cs::Bytes bytes;
  stream >> bytes;

  hash = csdb::PoolHash::from_binary(bytes);

  return stream;
}

///
/// Gets pool from stream.
///
inline DataStream& operator>>(DataStream& stream, csdb::Pool& pool) {
  cs::Bytes bytes;
  stream >> bytes;

  pool = csdb::Pool::from_binary(bytes);

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
    cserror() << "Data stream parsing of vector: nothing to parse";
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
/// Writes array to stream.
///
template <std::size_t size>
inline DataStream& operator<<(DataStream& stream, const std::array<char, size>& array) {
  stream.setStreamArray(array);
  return stream;
}

///
/// Writes T to stream.
///
template <typename T>
inline DataStream& operator<<(DataStream& stream, const T& streamField) {
  static_assert(std::is_trivial<T>::value, "Template parameter to must be trivial. Overload this function for non-trivial type");
  stream.setStreamField(streamField);
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
/// Writes byte array to stream.
///
template <std::size_t size>
inline DataStream& operator<<(DataStream& stream, const ByteArray<size>& array) {
  stream.setByteArray(array);
  return stream;
}

///
/// Writes hash binary to stream.
///
inline DataStream& operator<<(DataStream& stream, const cs::TransactionsPacketHash& hash) {
  stream.addTransactionsHash(hash);
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
  stream.setFixedString(fixedString);
  return stream;
}

///
/// Writes hash vector structure to stream.
///
inline DataStream& operator<<(DataStream& stream, const cs::HashVector& hashVector) {
  stream.addHashVector(hashVector);
  return stream;
}

///
/// Writes hash matrix structure to stream.
///
inline DataStream& operator<<(DataStream& stream, const cs::HashMatrix& hashMatrix) {
  stream.addHashMatrix(hashMatrix);
  return stream;
}

///
/// Writes hash matrix structure to stream.
///
inline DataStream& operator<<(DataStream& stream, const cs::TransactionsPacket& packet) {
  stream.addTransactionsPacket(packet);
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
}  // namespace cs

#endif  // DATASTREAM_HPP
