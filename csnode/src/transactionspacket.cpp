#include "csnode/transactionspacket.hpp"

#include <lz4.h>
#include <csdb/csdb.hpp>
#include <csdb/internal/utils.hpp>
#include <src/binary_streams.hpp>
#include <src/priv_crypto.hpp>

namespace cs {
//
// Static interface
//

TransactionsPacketHash TransactionsPacketHash::fromString(const ::std::string& str) {
    if (str.empty()) {
        return TransactionsPacketHash();
    }

    TransactionsPacketHash res;
    const cs::Bytes hash = ::csdb::internal::from_hex(str);

    if (::csdb::priv::crypto::hash_size == hash.size()) {
        res.m_bytes = hash;
    }

    return res;
}

TransactionsPacketHash TransactionsPacketHash::fromBinary(const cs::Bytes& data) {
    const size_t size = data.size();
    TransactionsPacketHash hash;

    if (::csdb::priv::crypto::hash_size == size) {
        hash.m_bytes = data;
    }

    return hash;
}

TransactionsPacketHash TransactionsPacketHash::fromBinary(cs::Bytes&& data) {
    const size_t size = data.size();
    TransactionsPacketHash hash;

    if (::csdb::priv::crypto::hash_size == size) {
        hash.m_bytes = std::move(data);
    }

    return hash;
}

TransactionsPacketHash TransactionsPacketHash::calcFromData(const cs::Bytes& data) {
    TransactionsPacketHash resHash;
    resHash.m_bytes = ::csdb::priv::crypto::calc_hash(data);
    return resHash;
}

//
// Interface
//

bool TransactionsPacketHash::isEmpty() const noexcept {
    return m_bytes.empty();
}

size_t TransactionsPacketHash::size() const noexcept {
    return m_bytes.size();
}

std::string TransactionsPacketHash::toString() const noexcept {
    return csdb::internal::to_hex(m_bytes.begin(), m_bytes.end());
}

const cs::Bytes& TransactionsPacketHash::toBinary() const noexcept {
    return m_bytes;
}

bool TransactionsPacketHash::operator==(const TransactionsPacketHash& other) const noexcept {
    return m_bytes == other.m_bytes;
}

bool TransactionsPacketHash::operator!=(const TransactionsPacketHash& other) const noexcept {
    return !operator==(other);
}

bool TransactionsPacketHash::operator<(const TransactionsPacketHash& other) const noexcept {
    return m_bytes < other.m_bytes;
}

//
// Static interface
//

TransactionsPacket TransactionsPacket::fromBinary(const cs::Bytes& data) {
    return fromByteStream(reinterpret_cast<const char*>(data.data()), data.size());
}

TransactionsPacket TransactionsPacket::fromByteStream(const char* data, size_t size) {
    ::csdb::priv::ibstream is(data, size);

    TransactionsPacket res;

    if (!res.get(is)) {
        return TransactionsPacket();
    }

    res.makeHash();
    return res;
}

TransactionsPacket::TransactionsPacket(TransactionsPacket&& packet)
: m_hash(std::move(packet.m_hash))
, m_transactions(std::move(packet.m_transactions))
, m_stateTransactions(std::move(packet.m_stateTransactions))
, m_signatures(std::move(packet.m_signatures)) {
    packet.m_hash = TransactionsPacketHash();
    packet.m_transactions.clear();
}

TransactionsPacket& TransactionsPacket::operator=(const TransactionsPacket& packet) {
    if (this == &packet) {
        return *this;
    }

    m_hash = packet.m_hash;
    m_transactions = packet.m_transactions;
    m_signatures = packet.m_signatures;
    m_stateTransactions = packet.m_stateTransactions;

    return *this;
}

//
// Interface
//

cs::Bytes TransactionsPacket::toBinary(Serialization options) const noexcept {
    ::csdb::priv::obstream os;
    put(os, options);
    return os.buffer();
}

bool TransactionsPacket::makeHash() {
    bool isEmpty = isHashEmpty();

    if (isEmpty) {
        m_hash = TransactionsPacketHash::calcFromData(toBinary(Serialization::Transactions));
    }

    return isEmpty;
}

bool TransactionsPacket::isHashEmpty() const noexcept {
    return m_hash.isEmpty();
}

const TransactionsPacketHash& TransactionsPacket::hash() const noexcept {
    return m_hash;
}

size_t TransactionsPacket::transactionsCount() const noexcept {
    return m_transactions.size();
}

bool TransactionsPacket::addTransaction(const csdb::Transaction& transaction) {
    if (!transaction.is_valid() || !isHashEmpty()) {
        return false;
    }

    m_transactions.push_back(transaction);
    return true;
}

bool TransactionsPacket::addStateTransaction(const csdb::Transaction& transaction) {
    if (!transaction.is_valid() || !isHashEmpty()) {
        return false;
    }

    m_stateTransactions.push_back(transaction);
    return true;
}

bool TransactionsPacket::addSignature(const cs::Byte index, const cs::Signature& signature) {
    auto iter = std::find_if(m_signatures.begin(), m_signatures.end(), [&](const auto& element) { return index == element.first; });

    if (iter != m_signatures.end()) {
        return false;
    }

    m_signatures.push_back(std::make_pair(index, signature));
    return true;
}

bool TransactionsPacket::sign(const cs::PrivateKey& privKey) {
    
    if (m_signatures.size() > 0) {
        return false;
    }

    if (isHashEmpty()) {
        return false;
    }

    cs::Signature sig;
    sig = cscrypto::generateSignature(privKey, m_hash.toBinary().data(), m_hash.toBinary().size());

    m_signatures.push_back(std::make_pair(0U, sig));
    return true;
}

bool TransactionsPacket::verify(const cs::PublicKey& pubKey) {

    if (isHashEmpty()) {

        return false;
    }

    if (m_signatures.size() == 1) {
        return cscrypto::verifySignature(m_signatures.back().second.data(), pubKey.data(), m_hash.toBinary().data(), m_hash.toBinary().size());
    }
    else {
        return false;
    }
}

bool TransactionsPacket::verify(const std::vector<cs::PublicKey>& pubKeys) {

    if (isHashEmpty()) {

        return false;
    }

    if (m_signatures.size() < 3) {
        return false;
    }
    else  {
        size_t cnt = 0;
        for (auto it : m_signatures) {
            if (it.first < pubKeys.size()) {
                if (cscrypto::verifySignature(it.second.data(), pubKeys.at(it.first).data(), m_hash.toBinary().data(), m_hash.toBinary().size())) {
                    ++cnt;
                }
            }
            else {
                return false;
            }

        }
        if (cnt > pubKeys.size() / 2) {
            return true;
        }
        else {
            return false;
        }

    }

}

const cs::BlockSignatures& TransactionsPacket::signatures() const noexcept {
    return m_signatures;
}

const std::vector<csdb::Transaction>& TransactionsPacket::transactions() const noexcept {
    return m_transactions;
}

const std::vector<csdb::Transaction>& TransactionsPacket::stateTransactions() const noexcept {
    return m_stateTransactions;
}

std::vector<csdb::Transaction>& TransactionsPacket::transactions() {
    return m_transactions;
}

void TransactionsPacket::clear() noexcept {
    m_transactions.clear();
}

//
// Service
//

void TransactionsPacket::put(::csdb::priv::obstream& os, Serialization options) const {
    if (options & Serialization::Transactions) {
        os.put(m_transactions.size());

        for (const auto& it : m_transactions) {
            os.put(it);
        }
    }

    if (options & Serialization::States) {
        os.put(m_stateTransactions.size());

        for (const auto& state : m_stateTransactions) {
            os.put(state);
        }
    }

    if (options & Serialization::Signatures) {
        os.put(m_signatures.size());

        for (const auto& it : m_signatures) {
            os.put(it.first);
            os.put(it.second);
        }
    }
}

bool TransactionsPacket::get(::csdb::priv::ibstream& is) {
    std::size_t transactionsCount = 0;

    if (!is.get(transactionsCount)) {
        return false;
    }

    m_transactions.clear();
    m_transactions.reserve(transactionsCount);

    for (std::size_t i = 0; i < transactionsCount; ++i) {
        csdb::Transaction transaction;

        if (!is.get(transaction)) {
            return false;
        }

        m_transactions.push_back(transaction);
    }

    std::size_t statesCount = 0;

    if (is.get(statesCount)) {
        m_stateTransactions.clear();
        m_stateTransactions.reserve(statesCount);

        for (std::size_t i = 0; i < statesCount; ++i) {
            csdb::Transaction state;

            if (!is.get(state)) {
                return false;
            }

            m_stateTransactions.push_back(state);
        }
    }

    std::size_t signaturesCount = 0;

    if (is.get(signaturesCount)) {
        m_signatures.clear();
        m_signatures.reserve(signaturesCount);

        for (std::size_t i = 0; i < signaturesCount; ++i) {
            cs::Byte index;
            cs::Signature signature;

            if (!is.get(index)) {
                return false;
            }

            if (!is.get(signature)) {
                return false;
            }

            m_signatures.push_back(std::make_pair(index, signature));
        }
    }

    return true;
}
}  // namespace cs
