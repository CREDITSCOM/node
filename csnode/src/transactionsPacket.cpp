#include "csnode/transactionsPacket.h"

#include <csdb/internal/utils.h>
#include <lz4.h>
#include <csdb/csdb.h>
#include "../csdb/src/binary_streams.h"
#include "../csdb/src/priv_crypto.h"
#include "csnode/dynamicbuffer.h"

namespace cs
{
    //
    // Static interface
    //

    TransactionsPacketHash TransactionsPacketHash::fromString(const ::std::string& str)
    {
        if (str.size() == 0)
            return TransactionsPacketHash();

        TransactionsPacketHash             res;
        const ::csdb::internal::byte_array hash = ::csdb::internal::from_hex(str);

        if (::csdb::priv::crypto::hash_size == hash.size())
            res.m_bytes = hash;

        return res;
    }

    TransactionsPacketHash TransactionsPacketHash::fromBinary(const internal::Bytes& data)
    {
        const size_t size = data.size();
        TransactionsPacketHash resHash;

        if ((0 == size) ||
            (::csdb::priv::crypto::hash_size == size))
        {
            resHash.m_bytes = data;
        }

        return resHash;
    }

    TransactionsPacketHash TransactionsPacketHash::calcFromData(const internal::Bytes& data)
    {
        TransactionsPacketHash resHash;
        resHash.m_bytes = ::csdb::priv::crypto::calc_hash(data);
        return resHash;
    }

    //
    // Interface
    //

    bool TransactionsPacketHash::isEmpty() const noexcept
    {
        return m_bytes.empty();
    }

    size_t TransactionsPacketHash::size() const noexcept
    {
        return m_bytes.size();
    }

    std::string TransactionsPacketHash::toString() const noexcept
    {
        return csdb::internal::to_hex(m_bytes.begin(), m_bytes.end());
    }

    const internal::Bytes& TransactionsPacketHash::toBinary() const noexcept
    {
        return m_bytes;
    }

    bool TransactionsPacketHash::operator == (const TransactionsPacketHash& other) const noexcept
    {
        return (m_bytes == other.m_bytes) || (m_bytes == other.m_bytes);
    }

    bool TransactionsPacketHash::operator != (const TransactionsPacketHash& other) const noexcept
    {
        return !operator ==(other);
    }

    bool TransactionsPacketHash::operator < (const TransactionsPacketHash& other) const noexcept
    {
        return (m_bytes != other.m_bytes) && (m_bytes < other.m_bytes);
    }



    //
    // Static interface
    //

    TransactionsPacket TransactionsPacket::fromBinary(const internal::Bytes& data)
    {
        return fromByteStream((const char*)(data.data()), data.size());
    }

    TransactionsPacket TransactionsPacket::fromByteStream(const char* data, size_t size)
    {
        ::csdb::priv::ibstream is(data, size);

        TransactionsPacket res;

        if (!res.get(is))
            return TransactionsPacket();

        res.makeHash();

        return res;
    }

    //
    // Interface
    //

    internal::Bytes TransactionsPacket::toBinary() const noexcept
    {
        ::csdb::priv::obstream os;
        put(os);
        return os.buffer();
    }

    bool TransactionsPacket::makeHash()
    {
        bool isEmpty = isHashEmpty();

        if (isEmpty)
            m_hash = TransactionsPacketHash::calcFromData(toBinary());

        return isEmpty;
    }

    bool TransactionsPacket::isHashEmpty() const noexcept
    {
        return m_hash.isEmpty();
    }

    const TransactionsPacketHash& TransactionsPacket::hash() const noexcept
    {
        return m_hash;
    }

    size_t TransactionsPacket::transactionsCount() const noexcept
    {
        return m_transactions.size();
    }

    bool TransactionsPacket::addTransaction(const csdb::Transaction& transaction)
    {
        if (!transaction.is_valid() &&
            !isHashEmpty())
        {
            return false;
        }

        m_transactions.push_back(transaction);

        return true;
    }

    const std::vector<csdb::Transaction>& TransactionsPacket::transactions() const noexcept
    {
        return m_transactions;
    }

    void TransactionsPacket::clear() noexcept
    {
        m_transactions.clear();
    }

    //
    // Service
    //

    void TransactionsPacket::put(::csdb::priv::obstream& os) const
    {
        os.put(m_transactions.size());

        for (const auto& it : m_transactions)
            os.put(it);
    }

    bool TransactionsPacket::get(::csdb::priv::ibstream& is)
    {
        size_t count;

        if (!is.get(count))
            return false;

        m_transactions.clear();
        m_transactions.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            csdb::Transaction tran;

            if (!is.get(tran))
                return false;

            m_transactions.push_back(tran);
        }

        return true;
    }
}
