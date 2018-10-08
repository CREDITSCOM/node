#ifndef DATASTREAM_H
#define DATASTREAM_H

#include <boost/asio/ip/udp.hpp>
#include <exception>
#include <string>
#include <csnode/nodecore.h>
#include <algorithm>
#include <type_traits>

#include <lib/system/common.hpp>
#include <lib/system/structures.hpp>

namespace cs
{
    ///
    /// Exception for packet stream.
    ///
    class DataStreamException : public std::exception
    {
    public:
        explicit DataStreamException(const std::string& message);
        virtual const char* what() const noexcept override;

    private:
        const std::string m_message;
    };

    ///
    /// The Data stream class represents an entity that controls data from any char array.
    ///
    class DataStream
    {
    public:
        explicit DataStream(char* packet, std::size_t dataSize);
        explicit DataStream(const char* packet, std::size_t dataSize);
        explicit DataStream(const uint8_t* packet, std::size_t dataSize);

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
        template<typename T>
        inline T streamField()
        {
            if (!isAvailable(sizeof(T)))
                return T();

            T field = getFromArray<T>(m_data, m_index);
            m_index += sizeof(T);
            return field;
        }

        ///
        /// Try to add field to stream.
        ///
        /// @param streamField Added type.
        ///
        template<typename T>
        inline void setStreamField(const T& streamField)
        {
            if (!isAvailable(sizeof(T)))
                return;

            insertToArray(m_data, m_index, streamField);
            m_index += sizeof(T);
        }

        ///
        /// Returns char array from stream.
        ///
        /// @return Returns char array.
        /// If stream can not return valid array than returns empty char array.
        ///
        template<std::size_t size>
        inline std::array<char, size> streamArray()
        {
            std::array<char, size> array = {0};

            if (!isAvailable(size))
                return array;

            for (std::size_t i = 0; i < size; ++i)
                array[i] = m_data[i + m_index];

            m_index += size;
            return array;
        }

        ///
        /// Adds char array to data stream.
        ///
        /// @param array Char array.
        /// If stream can not add char array to stream than method does nothing.
        ///
        template<std::size_t size>
        inline void setStreamArray(const std::array<char, size>& array)
        {
            if (!isAvailable(size))
                return;

            for (std::size_t i = 0; i < size; ++i)
                m_data[i + m_index] = array[i];

            m_index += size;
        }

        ///
        /// Adds array to stream.
        ///
        /// @param array Byte array.
        /// If stream can not add byte array to strem than method does nothing.
        ///
        template<std::size_t size>
        inline void setByteArray(const ByteArray<size>& array)
        {
            std::array<char, size> transformed;

            std::transform(array.begin(), array.end(), transformed.begin(), [](const auto element) {
                return static_cast<char>(element);
            });

            setStreamArray(transformed);
        }

        ///
        /// Returns static byte array.
        ///
        /// @return Returns byte array.
        /// If stream can not returns valid byte array it returns empty array.
        ///
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

        ///
        /// Adds fixed string to stream.
        ///
        /// @param fixedString Template FixedString.
        ///
        template<std::size_t size>
        inline void setFixedString(const FixedString<size>& fixedString)
        {
            if (!isAvailable(size))
                return;

            for (std::size_t i = 0; i < size; ++i)
                m_data[i + m_index] = fixedString[i];

            m_index += size;
        }

        ///
        /// Returns fixed string by template size.
        ///
        /// @return Returns template FixedString.
        /// If stream can not return available bytes size it returns zero FixedString.
        ///
        template<std::size_t size>
        inline FixedString<size> fixedString()
        {
            FixedString<size> str;

            if (!isAvailable(size))
                return str;

            for (std::size_t i = 0; i < size; ++i)
                str[i] = m_data[i + m_index];

            m_index += size;

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
        template<std::size_t size>
        inline void skip()
        {
            if (!isAvailable(size))
                return;

            m_index += size;
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
        void addVector(const std::vector<uint8_t>& data);

        ///
        /// Returns bytes vector.
        ///
        /// @param size Stream would take size of arg to form return vector.
        /// @return Returns byte vector.
        /// If stream can not return size of bytes it returns empty vector.
        ///
        std::vector<uint8_t> byteVector(std::size_t size);

        ///
        /// Adds std::string chars to stream.
        ///
        /// @param string Any information represented as std::string.
        ///
        void addString(const std::string& string);

        ///
        /// Returns std::string from stream.
        ///
        /// @param size Stream would take size to create string size.
        /// @return Returns std::string by arguments size.
        /// If stream can not return size of bytes it returns empty std::string.
        ///
        std::string string(std::size_t size);

        ///
        /// Peeks next parameter
        ///
        /// @return Returns next T parameter
        ///
        template <typename T>
        inline const T& peek() const
        {
            return *(reinterpret_cast<T*>(m_data + m_index));
        }

    private:

        // attributes
        char* m_data = nullptr;
        char* m_head = nullptr;

        std::size_t m_index = 0;
        std::size_t m_dataSize = 0;

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

    ///
    /// Gets next end point from stream to end point variable.
    ///
    inline DataStream& operator>>(DataStream& stream, boost::asio::ip::udp::endpoint& endPoint)
    {
        endPoint = stream.endpoint();
        return stream;
    }

    ///
    /// Gets from stream to uint8_t variable.
    ///
    template<typename T>
    inline DataStream& operator>>(DataStream& stream, T& streamField)
    {
        static_assert(std::is_trivial<T>::value, "Template parameter to must be trivial. Overload this function for non-trivial type");
        streamField = stream.streamField<T>();
        return stream;
    }

    ///
    /// Gets from stream to array.
    ///
    template<std::size_t size>
    inline DataStream& operator>>(DataStream& stream, std::array<char, size>& array)
    {
        array = stream.streamArray<size>();
        return stream;
    }

    ///
    /// Gets from stream to byte array.
    ///
    template<std::size_t size>
    inline DataStream& operator>>(DataStream& stream, ByteArray<size>& array)
    {
        array = stream.byteArray<size>();
        return stream;
    }

    ///
    /// Gets from stream to transactions packet hash.
    ///
    inline DataStream& operator>>(DataStream& stream, cs::TransactionsPacketHash& hash)
    {
        hash = stream.transactionsHash();
        return stream;
    }

    ///
    /// Gets from stream to bytes vector (stream would use data size of vector to create bytes).
    ///
    inline DataStream& operator>>(DataStream& stream, std::vector<uint8_t>& data)
    {
        data = stream.byteVector(data.size());
        return stream;
    }

    ///
    /// Gets from stream to std::string (stream would use data size of string to create bytes).
    ///
    inline DataStream& operator>>(DataStream& stream, std::string& data)
    {
        data = stream.string(data.size());
        return stream;
    }

    ///
    /// Get size of bytes from stream to fixedString
    ///
    template<std::size_t size>
    inline DataStream& operator>>(DataStream& stream, FixedString<size>& fixedString)
    {
        fixedString = stream.fixedString<size>();
        return stream;
    }

    ///
    /// Writes array to stream.
    ///
    template<std::size_t size>
    inline DataStream& operator<<(DataStream& stream, const std::array<char, size>& array)
    {
        stream.setStreamArray(array);
        return stream;
    }

    ///
    /// Writes T to stream.
    ///
    template<typename T>
    inline DataStream& operator<<(DataStream& stream, const T& streamField)
    {
        static_assert(std::is_trivial<T>::value, "Template parameter to must be trivial. Overload this function for non-trivial type");
        stream.setStreamField(streamField);
        return stream;
    }

    ///
    /// Writes address to stream.
    ///
    inline DataStream& operator<<(DataStream& stream, const boost::asio::ip::udp::endpoint& endpoint)
    {
        stream.addEndpoint(endpoint);
        return stream;
    }

    ///
    /// Writes byte array to stream.
    ///
    template<std::size_t size>
    inline DataStream& operator<<(DataStream& stream, const ByteArray<size>& array)
    {
        stream.setByteArray(array);
        return stream;
    }

    ///
    /// Writes hash binary to stream.
    ///
    inline DataStream& operator<<(DataStream& stream, const cs::TransactionsPacketHash& hash)
    {
        stream.addTransactionsHash(hash);
        return stream;
    }

    ///
    /// Writes vector of bytes to stream.
    ///
    inline DataStream& operator<<(DataStream& stream, const std::vector<uint8_t>& data)
    {
        stream.addVector(data);
        return stream;
    }

    ///
    /// Writes std::string to stream.
    ///
    inline DataStream& operator<<(DataStream& stream, const std::string& data)
    {
        stream.addString(data);
        return stream;
    }

    ///
    /// Writes fixed string to stream
    ///
    template<std::size_t size>
    inline DataStream& operator<<(DataStream& stream, const FixedString<size>& fixedString)
    {
        stream.setFixedString(fixedString);
        return stream;
    }
}

#endif // DATASTREAM_H

