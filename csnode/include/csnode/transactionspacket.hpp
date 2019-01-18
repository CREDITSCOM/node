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

  ///
  /// @brief Calculates hash from binary data
  /// @param vector of bytes
  /// @return hash
  ///
  static TransactionsPacketHash calcFromData(const cs::Bytes& data);

public:  // Interface
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
  cs::Bytes m_bytes;
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
  TransactionsPacket() = default;

  TransactionsPacket(const TransactionsPacket& packet) = default;
  TransactionsPacket(TransactionsPacket&& packet);

  TransactionsPacket& operator=(const TransactionsPacket& packet);

  ///
  /// @brief Coverts transactions packet to binary representation.
  /// @return packet as binary representation
  ///
  cs::Bytes toBinary() const noexcept;

  cs::Bytes toHashBinary() const noexcept;

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
  /// @brief Adds signature to transaction vector
  /// @param signature Signature to add
  ///
  bool addSignature(const cscrypto::Signature& signature);

  ///
/// @brief Adds transaction to transaction vector
/// @param transaction Any transaction to add
///
  bool addTransaction(const csdb::Transaction& transaction);

  ///
  /// @brief Returns transactions
  /// @return Reference to transactions vector
  ///
  const std::vector<csdb::Transaction>& transactions() const noexcept;

  ///
/// @brief Returns transactions
/// @return Reference to signatures vector
///
  const std::vector<cscrypto::Signature>& signatures() const noexcept;

  ///
  /// @brief Returns trabsactions, non const version
  /// @return Reference to transactions vector
  ///
  std::vector<csdb::Transaction>& transactions();

  ///
  /// @brief Clears transactions vector
  ///
  void clear() noexcept;

private:  // Service
  void hashPut(::csdb::priv::obstream& os) const;
  void put(::csdb::priv::obstream& os) const;
  bool get(::csdb::priv::ibstream& is);

private:  // Members
  TransactionsPacketHash m_hash;
  std::vector<cscrypto::Signature> m_signatures;
  std::vector<csdb::Transaction> m_transactions;
};
}  // namespace cs

#endif  // TRANSACTIONS_PACKET_HPP
