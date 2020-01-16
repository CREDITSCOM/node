#ifndef TRANSACTIONS_PACKET_HPP
#define TRANSACTIONS_PACKET_HPP

#include <csdb/transaction.hpp>
#include <lib/system/common.hpp>

#include <string>
#include <vector>

namespace cs {
///
/// Wrapper of std::vector<uint8_t> to represent hash
///
class TransactionsPacketHash {
public:  // Static interface
    ///
    /// @brief Gets a hash from a string representation
    /// @param String representation of a hash
    /// @return Hash obtained from a string representation.
    ///         If the string representation is incorrect, an empty hash is returned.
    ///
    static TransactionsPacketHash fromString(const ::std::string& str);

    ///
    /// @brief Gets a hash from a binary representation
    /// @param Binary representation of a hash
    /// @return Hash obtained from a binary representation.
    ///         If the binary representation is incorrect, an empty hash is returned.
    ///
    static TransactionsPacketHash fromBinary(const cs::Bytes& data);
    static TransactionsPacketHash fromBinary(cs::Bytes&& data);

    ///
    /// @brief Calculates hash from binary data
    /// @param vector of bytes
    /// @return hash
    ///
    static TransactionsPacketHash calcFromData(const cs::Bytes& data);

public:  // Interface
    TransactionsPacketHash() = default;
    TransactionsPacketHash(const TransactionsPacketHash&) = default;
    TransactionsPacketHash(TransactionsPacketHash&&) = default;

    TransactionsPacketHash& operator=(const TransactionsPacketHash&) = default;
    TransactionsPacketHash& operator=(TransactionsPacketHash&&) = default;

    ///
    /// @brief Сhecks hash size bytes on 0
    /// @return true if hash size == 0
    ///
    bool isEmpty() const noexcept;

    ///
    /// @brief Returns hash bytes count
    /// @return hash bytes count
    ///
    size_t size() const noexcept;

    ///
    /// @brief Coverts transactions packet hash to string.
    /// @return hash bytes as string
    ///
    std::string toString() const noexcept;

    ///
    /// @brief Coverts transactions packet hash to binary.
    /// @return vector of bytes
    ///
    const cs::Bytes& toBinary() const noexcept;

    bool operator==(const TransactionsPacketHash& other) const noexcept;
    bool operator!=(const TransactionsPacketHash& other) const noexcept;
    bool operator<(const TransactionsPacketHash& other) const noexcept;

private:  // Members
    cs::Bytes bytes_;
};

///
/// Flexible strorage for transactions
///
class TransactionsPacket {
public:  // Static interface
    ///
    /// @brief Gets a transactions packet from a binary representation.
    /// @param Binary representation of a packet.
    /// @return Hash obtained from a binary representation.
    ///         If the binary representation is incorrect, an empty packet is returned.
    ///
    static TransactionsPacket fromBinary(const cs::Bytes& data);

    ///
    /// @brief Gets a transactions packet from a binary representation
    /// @param Binary representation of a transactions packet
    /// @param Binary representation size
    /// @return Hash obtained from a binary representation.
    ///         If the binary representation is incorrect, an empty packet is returned.
    ///
    static TransactionsPacket fromByteStream(const char* data, size_t size);

public:  // Interface
    enum Serialization : cs::Byte {
        Transactions = 0x01,
        States = 0x02,
        Signatures = 0x04,

        SignaturesAndStates = Signatures | States,
        All = Serialization::SignaturesAndStates | Serialization::Transactions
    };

    TransactionsPacket() = default;

    TransactionsPacket(const TransactionsPacket& packet) = default;
    TransactionsPacket(TransactionsPacket&& packet);

    TransactionsPacket& operator=(const TransactionsPacket& packet);

    ///
    /// @brief Coverts transactions packet to binary representation.
    /// @return packet as binary representation
    ///
    cs::Bytes toBinary(Serialization options = Serialization::All) const noexcept;

    ///
    /// @brief Generates hash
    /// @return True if hash generated successed
    ///
    bool makeHash();

    ///
    /// @brief Checks on hash empty state
    /// @return True if hash is empty
    ///
    bool isHashEmpty() const noexcept;

    ///
    /// @brief Returns packet hash
    /// @return Packet hash, check it on empty state
    ///
    const TransactionsPacketHash& hash() const noexcept;

    ///
    /// @brief Returns transactions count
    /// @return Size of transactions vector
    ///
    size_t transactionsCount() const noexcept;

    ///
    /// @brief Returns max packet lifetime round
    ///
    cs::RoundNumber expiredRound() const noexcept;

    ///
    /// @brief Adds signature to transaction vector
    /// @param signature Signature to add
    ///
    bool addSignature(const cs::Byte index, const cs::Signature& signature);

    ///
    /// @brief Sets max packet lifetime round
    ///
    void setExpiredRound(cs::RoundNumber round);

    bool sign(const cs::PrivateKey& privateKey);
    std::string verify(const cs::PublicKey& publicKey);
    std::string verify(const std::vector<cs::PublicKey>& publicKeys);

    ///
    /// @brief Adds transaction to transaction vector
    /// @param transaction Any transaction to add
    ///
    bool addTransaction(const csdb::Transaction& transaction);


    ///
    /// @brief Adds state transaction to packet
    /// @param transaction Any transaction to add
    ///
    bool addStateTransaction(const csdb::Transaction& transaction);

    ///
    /// @brief Returns transactions
    /// @return Reference to transactions vector
    ///
    const std::vector<csdb::Transaction>& transactions() const noexcept;

    ///
    /// @brief Returns transactions
    /// @return Reference to signatures vector
    ///
    const cs::BlockSignatures& signatures() const noexcept;

    ///
    /// @brief Returns trabsactions, non const version
    /// @return Reference to transactions vector
    ///
    std::vector<csdb::Transaction>& transactions();

    ///
    /// @brief Returns state trabsactions, non const version
    /// @return Reference to transactions vector
    ///
    const std::vector<csdb::Transaction>& stateTransactions() const noexcept;

    ///
    /// @brief Clears transactions vector
    ///
    void clear() noexcept;

    ///
    /// @brief Returns packet smart state
    ///
    bool isSmart() const;

private:  // Service
    void put(::csdb::priv::obstream& os, Serialization options) const;
    bool get(::csdb::priv::ibstream& is);

private:  // Members
    TransactionsPacketHash hash_;
    std::vector<csdb::Transaction> transactions_;
    std::vector<csdb::Transaction> stateTransactions_;
    cs::BlockSignatures signatures_;
    cs::RoundNumber expiredRound_{};
};
}  // namespace cs

#endif  // TRANSACTIONS_PACKET_HPP
