#include "../include/csnode/datastream.hpp"

const constexpr std::size_t v4Size = 4;
const constexpr std::size_t v6Size = 16;

cs::DataStreamException::DataStreamException(const std::string& message)
: message_(message) {
}

const char* cs::DataStreamException::what() const noexcept {
  return message_.c_str();
}

cs::DataStream::DataStream(char* packet, std::size_t dataSize)
: data_(packet)
, index_(0)
, dataSize_(dataSize) {
  head_ = data_;
}

cs::DataStream::DataStream(const char* packet, std::size_t dataSize)
: DataStream(const_cast<char*>(packet), dataSize) {
}

cs::DataStream::DataStream(const uint8_t* packet, std::size_t dataSize)
: DataStream(reinterpret_cast<const char*>(packet), dataSize) {
}

cs::DataStream::DataStream(cs::Bytes& storage)
: bytes_(&storage) {
}

boost::asio::ip::udp::endpoint cs::DataStream::endpoint() {
  char flags = *(data_ + index_);
  char v6 = flags & 1;
  char addressFlag = (flags >> 1) & 1;
  char portFlag = (flags >> 2) & 1;

  ++index_;

  std::size_t size = 0;

  if (addressFlag) {
    size += (v6) ? v6Size : v4Size;
  }

  if (portFlag) {
    size += sizeof(uint16_t);
  }

  boost::asio::ip::udp::endpoint point;
  boost::asio::ip::address address;
  uint16_t port = 0;

  if ((index_ + size) <= dataSize_) {
    if (addressFlag) {
      address = v6 ? boost::asio::ip::address(createAddress<boost::asio::ip::address_v6>())
                   : boost::asio::ip::address(createAddress<boost::asio::ip::address_v4>());
    }

    if (portFlag) {
      port = *(reinterpret_cast<uint16_t*>(data_ + index_));
      index_ += sizeof(uint16_t);
    }

    point = boost::asio::ip::udp::endpoint(address, port);
  }

  return point;
}

bool cs::DataStream::isValid() const {
  if (index_ >= dataSize_) {
    return false;
  }

  return true;
}

bool cs::DataStream::isAvailable(std::size_t size) {
  return (index_ + size) <= dataSize_;
}

char* cs::DataStream::data() const {
  if (!bytes_) {
    return head_;
  }
  else {
    return reinterpret_cast<char*>(bytes_->data());
  }
}

std::size_t cs::DataStream::size() const {
  if (!bytes_) {
    return dataSize_;
  }
  else {
    return bytes_->size();
  }
}

void cs::DataStream::addEndpoint(const boost::asio::ip::udp::endpoint& endpoint) {
  if (!bytes_) {
    return;
  }

  char v6 = endpoint.address().is_v6();
  bytes_->push_back((v6 | 6));

  if (v6) {
    boost::asio::ip::address_v6::bytes_type bytes = endpoint.address().to_v6().to_bytes();
    (*this) << bytes;
  }
  else {
    boost::asio::ip::address_v4::bytes_type bytes = endpoint.address().to_v4().to_bytes();
    (*this) << bytes;
  }

  (*this) << endpoint.port();
}

void cs::DataStream::addTransactionsHash(const cs::TransactionsPacketHash& hash) {
  (*this) << hash.toBinary();
}

cs::TransactionsPacketHash cs::DataStream::transactionsHash() {
  cs::Bytes bytes;
  (*this) >> bytes;

  return cs::TransactionsPacketHash::fromBinary(bytes);
}

void cs::DataStream::addVector(const cs::Bytes& data) {
  if (bytes_) {
    (*this) << data.size();
    bytes_->insert(bytes_->end(), data.begin(), data.end());
  }
}

cs::Bytes cs::DataStream::byteVector() {
  cs::Bytes result;

  if (!isAvailable(sizeof(std::size_t))) {
    return result;
  }

  std::size_t size;
  (*this) >> size;

  if (isAvailable(size)) {
    result = cs::Bytes(data_ + index_, data_ +index_ + size);
    index_ += size;
  }

  return result;
}

void cs::DataStream::addString(const std::string& string) {
  if (bytes_) {
    (*this) << string.size();
    bytes_->insert(bytes_->end(), string.begin(), string.end());
  }
}

std::string cs::DataStream::string() {
  std::string result;

  if (!isAvailable(sizeof(std::size_t))) {
    return result;
  }

  std::size_t size;
  (*this) >> size;

  if (isAvailable(size)) {
    result = std::string(data_ + index_ , data_ + index_ + size);
    index_ += size;
  }

  return result;
}

void cs::DataStream::addHashVector(const cs::HashVector& hashVector) {
  (*this) << hashVector.sender << hashVector.hash << hashVector.signature;
}

cs::HashVector cs::DataStream::hashVector() {
  cs::HashVector vector;

  (*this) >> vector.sender >> vector.hash >> vector.signature;

  return vector;
}

void cs::DataStream::addHashMatrix(const cs::HashMatrix& matrix) {
  (*this) << matrix.sender;

  for (std::size_t i = 0; i < hashVectorCount; ++i) {
    (*this) << matrix.hashVector[i];
  }

  (*this) << matrix.signature;
}

cs::HashMatrix cs::DataStream::hashMatrix() {
  cs::HashMatrix matrix;

  (*this) >> matrix.sender;

  for (std::size_t i = 0; i < hashVectorCount; ++i) {
    (*this) >> matrix.hashVector[i];
  }

  (*this) >> matrix.signature;

  return matrix;
}

void cs::DataStream::addTransactionsPacket(const cs::TransactionsPacket& packet) {
  (*this) << packet.toBinary();
}

cs::TransactionsPacket cs::DataStream::transactionPacket() {
  cs::Bytes bytes;
  (*this) >> bytes;

  return cs::TransactionsPacket::fromBinary(bytes);
}

void cs::DataStream::addBytesView(const cs::BytesView& bytesView) {
  if (bytes_) {
    (*this) << bytesView.size();
    insertBytes(bytesView.data(), bytesView.size());
  }
}

cs::BytesView cs::DataStream::bytesView() {
  cs::BytesView bytesView;
  size_t size = streamField<size_t>();

  if (isAvailable(size)) {
    bytesView = cs::BytesView(reinterpret_cast<cs::Byte*>(data_), size);
    index_ += size;
  }

  return bytesView;
}

template <typename T>
inline T cs::DataStream::createAddress() {
  typename T::bytes_type bytes;

  for (std::size_t i = 0; i < bytes.size(); ++i, ++index_) {
    bytes[i] = static_cast<unsigned char>(data_[index_]);
  }

  return T(bytes);
}
