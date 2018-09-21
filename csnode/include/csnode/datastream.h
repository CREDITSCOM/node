#ifndef DATASTREAM_H
#define DATASTREAM_H

#include <boost/asio/ip/udp.hpp>
#include <exception>
#include <string>
#include <nodecore.h>
#include <algorithm>

namespace cs
{
    /*!
        Exception for packet stream
    */
    class DataStreamException : public std::exception
    {
    public:
        explicit DataStreamException(const std::string& message);
        virtual const char* what() const noexcept override;

    private:
        const std::string mMessage;
    };

    /*!
        Static byte (unsigned char) array
    */
    template<std::size_t size>
    using ByteArray = std::array<unsigned char, size>;

    /*!
        The Data stream class represents an entity that controls data from any char array
    */
    class DataStream
    {
    public:
        explicit DataStream(char* packet, std::size_t dataSize);

        /*!
            Try to get enpoint from data

            @return Returns current end point from data
            If data stream can not return valid enpoint then returns empty enpoint
        */
        boost::asio::ip::udp::endpoint endpoint();

        /*!
            Returns current state of stream

            @return Returns state of stream
        */
        bool isValid() const;

        /*!
            Returns state of available bytes

            @param size Count of bytes
            @return Returns state of available bytes
        */
        bool isAvailable(std::size_t size);

        /*!
            Returns pointer to start of the data
        */
        char* data() const;

        /*!
            Try to get field from stream by sizeof(T)

            @return Returns stream field
            If stream can not return field than returns empty T()
        */
        template<typename T>
        inline T streamField()
        {
            if (!isAvailable(sizeof(T)))
                return T();

            T field = getFromArray<T>(mData, mIndex);
            mIndex += sizeof(T);
            return field;
        }

        /*!
            Try to add field to stream

            @param streamField Added type
        */
        template<typename T>
        inline void setStreamField(const T& streamField)
        {
            if (!isAvailable(sizeof(T)))
                return;

            insertToArray(mData, mIndex, streamField);
            mIndex += sizeof(T);
        }

        /*!
            Returns char array from stream

            @return Returns char array
            If stream can not return valid array than returns empty char array
        */
        template<std::size_t size>
        inline std::array<char, size> streamArray()
        {
            std::array<char, size> array = {0};

            if (!isAvailable(size))
                return array;

            for (std::size_t i = 0; i < size; ++i)
                array[i] = mData[i + mIndex];

            mIndex += size;
            return array;
        }

        /*!
            Adds char array to data stream

            @param array Char array
            If stream can not add char array to stream than method does nothing
        */
        template<std::size_t size>
        inline void setStreamArray(const std::array<char, size>& array)
        {
            if (!isAvailable(size))
                return;

            for (std::size_t i = 0; i < size; ++i)
                mData[i + mIndex] = array[i];

            mIndex += size;
        }

        /*!
            Adds array to stream

            @param array Byte array
            If stream can not add byte array to strem than method does nothing
        */
        template<std::size_t size>
        inline void setByteArray(const ByteArray<size>& array)
        {
            std::array<char, size> transformed;

            std::transform(array.begin(), array.end(), transformed.begin(), [](const auto element) {
                return static_cast<char>(element);
            });

            setStreamArray(transformed);
        }

        /*!
            Returns static byte array

            @return Returns byte array
            If stream can not returns valid byte array it returns empty array
        */
        template<std::size_t size>
        inline ByteArray<size> byteArray()
        {
            ByteArray<size> result;
            decltype(auto) array = streamArray<size>();

            std::transform(array.begin(), array.end(), result.begin(), [](const auto element) {
                return static_cast<unsigned char>(element);
            });

            return result;
        }

        /*!
            Returns byte size of write/read data

            @return Returns data stream size
        */
        std::size_t size() const;

        /*!
            Adds enpoint to stream

            @param enpoint Boost enpoint
        */
        void addEndpoint(const boost::asio::ip::udp::endpoint& endpoint);

        /*!
            Skips compile time size

            If stream can not skip size than it does nothing
        */
        template<std::size_t size>
        inline void skip()
        {
            if (!isAvailable(size))
                return;

            mIndex += size;
        }

        /*!
            Adds transactions packet hash to stream

            @param hash Data base transactions packet hash
        */
        void addTransactionsHash(const cs::TransactionsPacketHash& hash);

        /*!
            Returns packet hash
        */
        cs::TransactionsPacketHash transactionsHash();

        /*!
            Adds bytes vector to stream
        */
        void addVector(const std::vector<uint8_t>& data);

        /*!
            Returns bytes vector

            stream would take size of arg to form return vector
        */
        std::vector<uint8_t> byteVector(std::size_t size);

        /*!
            Adds std::string chars to stream
        */
        void addString(const std::string& string);

        /*!
            Returns std::string from stream

            stream would take size to create string size
        */
        std::string string(std::size_t size);

        /*!
            Peeks next parameter

            @return Returns next T parameter
        */
        template <typename T>
        inline const T& peek() const
        {
            return *(reinterpret_cast<T*>(mData + mIndex));
        }

    private:

        // attributes
        char* mData = nullptr;
        char* mHead = nullptr;

        std::size_t mIndex = 0;
        std::size_t mDataSize = 0;

        // creates template address
        template<typename T>
        T createAddress();

        template<typename T>
        inline void insertToArray(char* data, std::size_t index, T value)
        {
            char* ptr = reinterpret_cast<char*>(&value);

            for (std::size_t i = index, k = 0; i < index + sizeof(T); ++i, ++k)
                *(data + i) = *(ptr + k);
        }

        template<typename T>
        inline static T getFromArray(char* data, std::size_t index)
        {
            return *(reinterpret_cast<T*>(data + index));
        }
    };

    /*!
        Puts next end point from stream to end point variable
    */
    inline DataStream& operator>>(DataStream& stream, boost::asio::ip::udp::endpoint& endPoint)
    {
        endPoint = stream.endpoint();
        return stream;
    }

    /*!
        Puts from stream to uint8_t variable
    */
    template<typename T>
    inline DataStream& operator>>(DataStream& stream, T& streamField)
    {
        streamField = stream.streamField<T>();
        return stream;
    }

    /*!
        Puts from stream to array
    */
    template<std::size_t size>
    inline DataStream& operator>>(DataStream& stream, std::array<char, size>& array)
    {
        array = stream.streamArray<size>();
        return stream;
    }

    /*!
        Puts from stream to byte array
    */
    template<std::size_t size>
    inline DataStream& operator>>(DataStream& stream, ByteArray<size>& array)
    {
        array = stream.byteArray<size>();
        return stream;
    }

    /*!
        Puts from stream to transactions packet hash
    */
    inline DataStream& operator>>(DataStream& stream, cs::TransactionsPacketHash& hash)
    {
        hash = stream.transactionsHash();
        return stream;
    }

    /*!
        Puts from stream to bytes vector (stream would use data size of vector to create bytes)
    */
    inline DataStream& operator>>(DataStream& stream, std::vector<uint8_t>& data)
    {
        data = stream.byteVector(data.size());
        return stream;
    }

    /*!
        Puts from stream to std:;string (stream would use data size of string to create bytes)
    */
    inline DataStream& operator>>(DataStream& stream, std::string& data)
    {
        data = stream.string(data.size());
        return stream;
    }

    /*!
        Writes array to stream
    */
    template<std::size_t size>
    inline DataStream& operator<<(DataStream& stream, const std::array<char, size>& array)
    {
        stream.setStreamArray(array);
        return stream;
    }

    /*!
        Writes T to stream
    */
    template<typename T>
    inline DataStream& operator<<(DataStream& stream, const T& streamField)
    {
        stream.setStreamField(streamField);
        return stream;
    }

    /*!
        Writes address to stream
    */
    inline DataStream& operator<<(DataStream& stream, const boost::asio::ip::udp::endpoint& endpoint)
    {
        stream.addEndpoint(endpoint);
        return stream;
    }

    /*!
        Writes byte array to stream
    */
    template<std::size_t size>
    inline DataStream& operator<<(DataStream& stream, const ByteArray<size>& array)
    {
        stream.setByteArray(array);
        return stream;
    }

    /*!
        Writes hash binary to stream
    */
    inline DataStream& operator<<(DataStream& stream, const cs::TransactionsPacketHash& hash)
    {
        stream.addTransactionsHash(hash);
        return stream;
    }

    /*!
        Writes vector of bytes to stream
    */
    inline DataStream& operator<<(DataStream& stream, const std::vector<uint8_t>& data)
    {
        stream.addVector(data);
        return stream;
    }
}

#endif // DATASTREAM_H

