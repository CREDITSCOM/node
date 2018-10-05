#ifndef TRANSACTIONS_PACKET_H
#define TRANSACTIONS_PACKET_H

#include <vector>
#include <string>
#include <csdb/transaction.h>

namespace cs
{
    namespace internal
    {
        using byte  = uint8_t;
        using Bytes = std::vector<byte>;
    }

    ///
    /// Wrapper of std::vector<uint8_t> to represent hash
    ///
    class TransactionsPacketHash
    {
    public: // Static interface

        ///
        /// @brief  Gets a hash from a string representation
        /// @param  String representation of a hash
        /// @return Hash obtained from a string representation.
        ///         If the string representation is incorrect, an empty hash is returned.
        ///
        static TransactionsPacketHash fromString(const ::std::string& str);

        ///
        /// @brief  Gets a hash from a binary representation
        /// @param  Binary representation of a hash
        /// @return Hash obtained from a binary representation.
        ///         If the binary representation is incorrect, an empty hash is returned.
        ///
        static TransactionsPacketHash fromBinary(const internal::Bytes& data);

        ///
        /// @brief Calculates hash from binary data
        /// @param vector of bytes
        /// @return hash
        ///
        static TransactionsPacketHash calcFromData(const internal::Bytes& data);

    public: // Interface

        ///
        /// @brief Сhecks hash size bytes on 0
        /// @return true if hash size == 0
        ///
        bool isEmpty() const noexcept;

        ///
        /// @brief  Returns hash bytes count
        /// @return hash bytes count
        ///
        size_t size() const noexcept;

        ///
        /// @brief Coverts transactions packet hash to string.
        /// @return hash bytes as string
        ///
        std::string toString() const noexcept;

        ///
        /// @brief  Coverts transactions packet hash to binary.
        /// @return vector of bytes
        ///
        const internal::Bytes& toBinary() const noexcept;

        bool operator == (const TransactionsPacketHash& other) const noexcept;
        bool operator != (const TransactionsPacketHash& other) const noexcept;
        bool operator <  (const TransactionsPacketHash& other) const noexcept;

    private: // Members

        internal::Bytes m_bytes;
    };

    ///
    /// Flexible strorage for transactions
    ///
    class TransactionsPacket
    {
    public: // Static interface

        ///
        /// @brief  Gets a transactions packet from a binary representation.
        /// @param  Binary representation of a packet.
        /// @return Hash obtained from a binary representation.
        ///         If the binary representation is incorrect, an empty packet is returned.
		///
        static TransactionsPacket fromBinary(const internal::Bytes& data);

        ///
        /// @brief  Gets a transactions packet from a binary representation
        /// @param  Binary representation of a transactions packet
        /// @param  Binary representation size
        /// @return Hash obtained from a binary representation.
        ///         If the binary representation is incorrect, an empty packet is returned.
		///
        static TransactionsPacket fromByteStream(const char* data, size_t size);

    public: // Interface

        ///
        /// @brief  Coverts transactions packet to binary representation.
        /// @return packet as binary representation
        ///
        internal::Bytes toBinary() const noexcept;

        ///
        /// @brief Generates hash
        /// @return True if hash generated successed
        ///
        bool makeHash();

        bool isHashEmpty() const noexcept;

        const TransactionsPacketHash& hash() const noexcept;

        size_t transactionsCount() const noexcept;

        bool addTransaction(const csdb::Transaction& transaction);

        const std::vector<csdb::Transaction>& transactions() const noexcept;

        ///
        /// @brief transactions vector clear
        ///
        void clear() noexcept;

    private: // Service

        void put(::csdb::priv::obstream& os) const;
        bool get(::csdb::priv::ibstream& is);

    private: // Members

        TransactionsPacketHash         m_hash;
        std::vector<csdb::Transaction> m_transactions;
    };
}

#endif // TRANSACTIONS_PACKET_H
