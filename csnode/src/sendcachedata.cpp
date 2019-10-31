#include "sendcachedata.hpp"

namespace cs {
SendCacheData::SendCacheData()
: count_(0) {
}

SendCacheData::SendCacheData(const TransactionsPacketHash& hash)
: hash_(hash)
, count_(0) {
}

SendCacheData::SendCacheData(const TransactionsPacketHash& hash, size_t count)
: hash_(hash)
, count_(count) {
}
}  // namespace cs
