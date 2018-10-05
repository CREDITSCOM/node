#include "../include/csnode/datastream.h"

const constexpr std::size_t v4Size = 4;
const constexpr std::size_t v6Size = 16;

cs::DataStreamException::DataStreamException(const std::string& message):
    mMessage(message)
{
}

const char* cs::DataStreamException::what() const noexcept
{
    return mMessage.c_str();
}

cs::DataStream::DataStream(char* packet, std::size_t dataSize):
    mData(packet),
    mIndex(0),
    mDataSize(dataSize)
{
    mHead = mData;
}

cs::DataStream::DataStream(const char* packet, std::size_t dataSize):
    DataStream(const_cast<char*>(packet), dataSize)
{
}

cs::DataStream::DataStream(const uint8_t* packet, std::size_t dataSize):
    DataStream(reinterpret_cast<const char*>(packet), dataSize)
{
}

boost::asio::ip::udp::endpoint cs::DataStream::endpoint()
{
    char flags = *(mData + mIndex);
    char v6 = flags & 1;
    char addressFlag = (flags >> 1) & 1;
    char portFlag = (flags >> 2) & 1;

    ++mIndex;

    std::size_t size = 0;

    if (addressFlag)
        size += (v6) ? v6Size : v4Size;

    if (portFlag)
        size += sizeof(uint16_t);

    boost::asio::ip::udp::endpoint point;
    boost::asio::ip::address address;
    uint16_t port = 0;

    if ((mIndex + size) <= mDataSize)
    {
        if (addressFlag)
        {
            address = v6 ? boost::asio::ip::address(createAddress<boost::asio::ip::address_v6>()):
                        boost::asio::ip::address(createAddress<boost::asio::ip::address_v4>());
        }

        if (portFlag)
        {
            port = *(reinterpret_cast<uint16_t*>(mData + mIndex));
            mIndex += sizeof(uint16_t);
        }

        point = boost::asio::ip::udp::endpoint(address, port);
    }

    return point;
}

bool cs::DataStream::isValid() const
{
    if (mIndex >= mDataSize)
        return false;

    return true;
}

bool cs::DataStream::isAvailable(std::size_t size)
{
    return (mIndex + size) <= mDataSize;
}

char* cs::DataStream::data() const
{
    return mHead;
}

std::size_t cs::DataStream::size() const
{
    return mIndex;
}

void cs::DataStream::addEndpoint(const boost::asio::ip::udp::endpoint& endpoint)
{
    char v6 = endpoint.address().is_v6();
    mData[mIndex] = v6 | 6;

    if (v6)
    {
        boost::asio::ip::address_v6::bytes_type bytes = endpoint.address().to_v6().to_bytes();

        if ((mIndex + v6Size + sizeof(char) + sizeof(uint16_t)) > mDataSize)
            return;

        ++mIndex;

        for (std::size_t i = 0; i < bytes.size(); ++i, ++mIndex)
            mData[mIndex] = static_cast<char>(bytes[i]);
    }
    else
    {
        boost::asio::ip::address_v4::bytes_type bytes = endpoint.address().to_v4().to_bytes();

        if ((mIndex + v4Size + sizeof(char) + sizeof(uint16_t)) > mDataSize)
            return;

        ++mIndex;

        for (std::size_t i = 0; i < bytes.size(); ++i, ++mIndex)
            mData[mIndex] = static_cast<char>(bytes[i]);
    }

    *(reinterpret_cast<uint16_t*>(mData + mIndex)) = endpoint.port();
    mIndex += sizeof(uint16_t);
}

void cs::DataStream::addTransactionsHash(const cs::TransactionsPacketHash& hash)
{
    auto hashData = hash.toBinary();

    if (!isAvailable(hashData.size()))
        return;
    
    for (const auto item : hashData)
        setStreamField(item);
}

cs::TransactionsPacketHash cs::DataStream::transactionsHash()
{
    const std::size_t hashSize = 32;    // TODO: gag, look real project const
    cs::TransactionsPacketHash hash;

    if (!isAvailable(hashSize))
        return hash;

    csdb::internal::byte_array bytes;
    bytes.reserve(hashSize);

    for (std::size_t i = 0; i < hashSize; ++i)
        bytes.push_back(streamField<uint8_t>());

    return cs::TransactionsPacketHash::fromBinary(bytes);
}

void cs::DataStream::addVector(const std::vector<uint8_t>& data)
{
    if (!isAvailable(data.size()))
        return;

    for (std::size_t i = 0; i < data.size(); ++i, ++mIndex)
        mData[mIndex] = static_cast<char>(data[i]);
}

std::vector<uint8_t> cs::DataStream::byteVector(std::size_t size)
{
    std::vector<uint8_t> result;
    
    if (size == 0 && !isAvailable(size))
        return result;

    for (std::size_t i = 0; i < size; ++i, ++mIndex)
        result.push_back(static_cast<uint8_t>(mData[mIndex]));

    return result;
}

void cs::DataStream::addString(const std::string& string)
{
    if (!isAvailable(string.size()))
        return;

    for (std::size_t i = 0; i < string.size(); ++i, ++mIndex)
        mData[mIndex] = string[i];
}

std::string cs::DataStream::string(std::size_t size)
{
    std::string result;

    if (size == 0 && !isAvailable(size))
        return result;

    for (std::size_t i = 0; i < size; ++i, ++mIndex)
        result.push_back(mData[mIndex]);

    return result;
}

template<typename T>
inline T cs::DataStream::createAddress()
{
    typename T::bytes_type bytes;

    for (std::size_t i = 0; i < bytes.size(); ++i, ++mIndex)
        bytes[i] = static_cast<unsigned char>(mData[mIndex]);

    return T(bytes);
}
