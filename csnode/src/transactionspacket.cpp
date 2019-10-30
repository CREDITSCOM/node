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
        res.bytes_ = hash;
    }

    return res;
}

TransactionsPacketHash TransactionsPacketHash::fromBinary(const cs::Bytes& data) {
    const size_t size = data.size();
    TransactionsPacketHash hash;

    if (::csdb::priv::crypto::hash_size == size) {
        hash.bytes_ = data;
    }

    return hash;
}

TransactionsPacketHash TransactionsPacketHash::fromBinary(cs::Bytes&& data) {
    const size_t size = data.size();
    TransactionsPacketHash hash;

    if (::csdb::priv::crypto::hash_size == size) {
        hash.bytes_ = std::move(data);
    }

    return hash;
}

TransactionsPacketHash TransactionsPacketHash::calcFromData(const cs::Bytes& data) {
    TransactionsPacketHash resHash;
    resHash.bytes_ = ::csdb::priv::crypto::calc_hash(data);
    return resHash;
}

//
// Interface
//

bool TransactionsPacketHash::isEmpty() const noexcept {
    return bytes_.empty();
}

size_t TransactionsPacketHash::size() const noexcept {
    return bytes_.size();
}

std::string TransactionsPacketHash::toString() const noexcept {
    return csdb::internal::to_hex(bytes_.begin(), bytes_.end());
}

const cs::Bytes& TransactionsPacketHash::toBinary() const noexcept {
    return bytes_;
}

bool TransactionsPacketHash::operator==(const TransactionsPacketHash& other) const noexcept {
    return bytes_ == other.bytes_;
}

bool TransactionsPacketHash::operator!=(const TransactionsPacketHash& other) const noexcept {
    return !operator==(other);
}

bool TransactionsPacketHash::operator<(const TransactionsPacketHash& other) const noexcept {
    return bytes_ < other.bytes_;
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
: hash_(std::move(packet.hash_))
, transactions_(std::move(packet.transactions_))
, stateTransactions_(std::move(packet.stateTransactions_))
, signatures_(std::move(packet.signatures_)) {
    packet.hash_ = TransactionsPacketHash();
    packet.transactions_.clear();
}

TransactionsPacket& TransactionsPacket::operator=(const TransactionsPacket& packet) {
    if (this == &packet) {
        return *this;
    }

    hash_ = packet.hash_;
    transactions_ = packet.transactions_;
    signatures_ = packet.signatures_;
    stateTransactions_ = packet.stateTransactions_;

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
        hash_ = TransactionsPacketHash::calcFromData(toBinary(Serialization::Transactions));
    }

    return isEmpty;
}

bool TransactionsPacket::isHashEmpty() const noexcept {
    return hash_.isEmpty();
}

const TransactionsPacketHash& TransactionsPacket::hash() const noexcept {
    return hash_;
}

size_t TransactionsPacket::transactionsCount() const noexcept {
    return transactions_.size();
}

bool TransactionsPacket::addTransaction(const csdb::Transaction& transaction) {
    if (!transaction.is_valid() || !isHashEmpty()) {
        return false;
    }

    transactions_.push_back(transaction);
    return true;
}

bool TransactionsPacket::addStateTransaction(const csdb::Transaction& transaction) {
    if (!transaction.is_valid() || !isHashEmpty()) {
        return false;
    }

    stateTransactions_.push_back(transaction);
    return true;
}

bool TransactionsPacket::addSignature(const cs::Byte index, const cs::Signature& signature) {
    auto iter = std::find_if(signatures_.begin(), signatures_.end(), [&](const auto& element) {
        return index == element.first;
    });

    if (iter != signatures_.end()) {
        return false;
    }

    signatures_.push_back(std::make_pair(index, signature));
    return true;
}

bool TransactionsPacket::sign(const cs::PrivateKey& privateKey) {
    if (!privateKey) {
        return false;
    }

    if (signatures_.size() > 0) {
        return false;
    }

    if (isHashEmpty()) {
        return false;
    }

    signatures_.push_back(std::make_pair(cs::Byte(0), cscrypto::generateSignature(privateKey, hash_.toBinary().data(), hash_.toBinary().size())));
    return true;
}

bool TransactionsPacket::verify(const cs::PublicKey& publicKey) {
    if (isHashEmpty()) {
        return false;
    }

    if (signatures_.size() != 1) {
        return false;
    }

    return cscrypto::verifySignature(signatures_.back().second.data(), publicKey.data(), hash_.toBinary().data(), hash_.toBinary().size());
}

bool TransactionsPacket::verify(const std::vector<cs::PublicKey>& publicKeys) {
    if (isHashEmpty()) {
        return false;
    }

    if (signatures_.size() < 3) {
        return false;
    }

    size_t count = 0;

    for (auto it : signatures_) {
        if (it.first < publicKeys.size()) {
            if (cscrypto::verifySignature(it.second.data(), publicKeys[it.first].data(), hash_.toBinary().data(), hash_.toBinary().size())) {
                ++count;
            }
        }
        else {
            return false;
        }
    }

    return count > publicKeys.size() / 2;
}

const cs::BlockSignatures& TransactionsPacket::signatures() const noexcept {
    return signatures_;
}

const std::vector<csdb::Transaction>& TransactionsPacket::transactions() const noexcept {
    return transactions_;
}

const std::vector<csdb::Transaction>& TransactionsPacket::stateTransactions() const noexcept {
    return stateTransactions_;
}

std::vector<csdb::Transaction>& TransactionsPacket::transactions() {
    return transactions_;
}

void TransactionsPacket::clear() noexcept {
    transactions_.clear();
}

//
// Service
//

void TransactionsPacket::put(::csdb::priv::obstream& os, Serialization options) const {
    if (options & Serialization::Transactions) {
        os.put(transactions_.size());

        for (const auto& it : transactions_) {
            os.put(it);
        }
    }

    if (options & Serialization::States) {
        os.put(stateTransactions_.size());

        for (const auto& state : stateTransactions_) {
            os.put(state);
        }
    }

    if (options & Serialization::Signatures) {
        os.put(signatures_.size());

        for (const auto& it : signatures_) {
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

    transactions_.clear();
    transactions_.reserve(transactionsCount);

    for (std::size_t i = 0; i < transactionsCount; ++i) {
        csdb::Transaction transaction;

        if (!is.get(transaction)) {
            return false;
        }

        transactions_.push_back(transaction);
    }

    std::size_t statesCount = 0;

    if (is.get(statesCount)) {
        stateTransactions_.clear();
        stateTransactions_.reserve(statesCount);

        for (std::size_t i = 0; i < statesCount; ++i) {
            csdb::Transaction state;

            if (!is.get(state)) {
                return false;
            }

            stateTransactions_.push_back(state);
        }
    }

    std::size_t signaturesCount = 0;

    if (is.get(signaturesCount)) {
        signatures_.clear();
        signatures_.reserve(signaturesCount);

        for (std::size_t i = 0; i < signaturesCount; ++i) {
            cs::Byte index;
            cs::Signature signature;

            if (!is.get(index)) {
                return false;
            }

            if (!is.get(signature)) {
                return false;
            }

            signatures_.push_back(std::make_pair(index, signature));
        }
    }

    return true;
}
}  // namespace cs
