#ifndef SENDCACHEDATA_HPP
#define SENDCACHEDATA_HPP

#include <map>

#include <csnode/transactionspacket.hpp>

namespace cs {
class SendCacheData {
public:
    SendCacheData();
    explicit SendCacheData(const cs::TransactionsPacketHash& hash);
    explicit SendCacheData(const cs::TransactionsPacketHash& hash, size_t count);

    const cs::TransactionsPacketHash& hash() const {
        return hash_;
    }

    size_t count() const {
        return count_;
    }

private:
    cs::TransactionsPacketHash hash_;
    size_t count_;
};

// send transactions packet cache for conveyer
using TransactionPacketSendCache = std::multimap<cs::RoundNumber, SendCacheData>;
}  // namespace cs

#endif  // SENDCACHEDATA_HPP
