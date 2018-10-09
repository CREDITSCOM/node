#include "../include/csnode/datastream.h"

const constexpr std::size_t v4Size = 4;
const constexpr std::size_t v6Size = 16;

cs::DataStreamException::DataStreamException(const std::string& message):
    m_message(message)
{
}

const char* cs::DataStreamException::what() const noexcept
{
    return m_message.c_str();
}

cs::DataStream::DataStream(char* packet, std::size_t dataSize):
    m_data(packet),
    m_index(0),
    m_dataSize(dataSize)
{
    m_head = m_data;
}

cs::DataStream::DataStream(const char* packet, std::size_t dataSize):
    DataStream(const_cast<char*>(packet), dataSize)
{
}

cs::DataStream::DataStream(const uint8_t* packet, std::size_t dataSize):
    DataStream(reinterpret_cast<const char*>(packet), dataSize)
{
}

cs::DataStream::DataStream(cs::Bytes& storage):
    m_bytes(&storage)
{
}

boost::asio::ip::udp::endpoint cs::DataStream::endpoint()
{
    char flags = *(m_data + m_index);
    char v6 = flags & 1;
    char addressFlag = (flags >> 1) & 1;
    char portFlag = (flags >> 2) & 1;

    ++m_index;

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

    if ((m_index + size) <= m_dataSize)
    {
        if (addressFlag)
        {
            address = v6 ? boost::asio::ip::address(createAddress<boost::asio::ip::address_v6>()):
                        boost::asio::ip::address(createAddress<boost::asio::ip::address_v4>());
        }

        if (portFlag)
        {
            port = *(reinterpret_cast<uint16_t*>(m_data + m_index));
            m_index += sizeof(uint16_t);
        }

        point = boost::asio::ip::udp::endpoint(address, port);
    }

    return point;
}

bool cs::DataStream::isValid() const
{
    if (m_index >= m_dataSize) {
        return false;
    }

    return true;
}

bool cs::DataStream::isAvailable(std::size_t size)
{
    return (m_index + size) <= m_dataSize;
}

char* cs::DataStream::data() const
{
    if (!m_bytes) {
        return m_head;
    }
    else {
        return reinterpret_cast<char*>(m_bytes->data());
    }
}

std::size_t cs::DataStream::size() const
{
    if (!m_bytes) {
        return m_index;
    }
    else {
        return m_bytes->size();
    }
}

void cs::DataStream::addEndpoint(const boost::asio::ip::udp::endpoint& endpoint)
{
    // TODO: fix to m_bytes
    char v6 = endpoint.address().is_v6();
    m_data[m_index] = v6 | 6;

    if (v6)
    {
        boost::asio::ip::address_v6::bytes_type bytes = endpoint.address().to_v6().to_bytes();

        if ((m_index + v6Size + sizeof(char) + sizeof(uint16_t)) > m_dataSize) {
            return;
        }

        ++m_index;

        for (std::size_t i = 0; i < bytes.size(); ++i, ++m_index) {
            m_data[m_index] = static_cast<char>(bytes[i]);
        }
    }
    else
    {
        boost::asio::ip::address_v4::bytes_type bytes = endpoint.address().to_v4().to_bytes();

        if ((m_index + v4Size + sizeof(char) + sizeof(uint16_t)) > m_dataSize) {
            return;
        }

        ++m_index;

        for (std::size_t i = 0; i < bytes.size(); ++i, ++m_index) {
            m_data[m_index] = static_cast<char>(bytes[i]);
        }
    }

    *(reinterpret_cast<uint16_t*>(m_data + m_index)) = endpoint.port();
    m_index += sizeof(uint16_t);
}

void cs::DataStream::addTransactionsHash(const cs::TransactionsPacketHash& hash)
{
    auto hashData = hash.toBinary();
    
    (*this) << hashData;
}

cs::TransactionsPacketHash cs::DataStream::transactionsHash()
{
    const std::size_t hashSize = cs::HashLength;
    cs::TransactionsPacketHash hash;

    cs::Bytes bytes;
    bytes.resize(hashSize);

    (*this) >> bytes;

    return cs::TransactionsPacketHash::fromBinary(bytes);
}

void cs::DataStream::addVector(const cs::Bytes& data)
{
    if (m_bytes) {
        m_bytes->insert(m_bytes->end(), data.begin(), data.end());
    }
}

cs::Bytes cs::DataStream::byteVector(std::size_t size)
{
    cs::Bytes result;
    
    if (size == 0 && !isAvailable(size)) {
        return result;
    }

    for (std::size_t i = 0; i < size; ++i, ++m_index) {
        result.push_back(static_cast<uint8_t>(m_data[m_index]));
    }

    return result;
}

void cs::DataStream::addString(const std::string& string)
{
    if (m_bytes) {
        m_bytes->insert(m_bytes->end(), string.begin(), string.end());
    }
}

std::string cs::DataStream::string(std::size_t size)
{
    std::string result;

    if (size == 0 && !isAvailable(size)) {
        return result;
    }

    for (std::size_t i = 0; i < size; ++i, ++m_index) {
        result.push_back(m_data[m_index]);
    }

    return result;
}

void cs::DataStream::addHashVector(const cs::HashVector& hashVector)
{
    (*this) << hashVector.sender;
    (*this) << hashVector.hash;
    (*this) << hashVector.signature;
}

cs::HashVector cs::DataStream::hashVector()
{
    cs::HashVector vector;

    (*this) >> vector.sender;
    (*this) >> vector.hash;
    (*this) >> vector.signature;

    return vector;
}

template<typename T>
inline T cs::DataStream::createAddress()
{
    typename T::bytes_type bytes;

    for (std::size_t i = 0; i < bytes.size(); ++i, ++m_index)
        bytes[i] = static_cast<unsigned char>(m_data[m_index]);

    return T(bytes);
}
