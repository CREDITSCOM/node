#include "csdb/transaction.hpp"
#include "transaction_p.hpp"

#include <cinttypes>
#include <iomanip>
#include <iterator>
#include <sstream>

#include <cscrypto/cscrypto.hpp>
#include "binary_streams.hpp"
#include "csdb/address.hpp"
#include "csdb/amount.hpp"
#include "csdb/currency.hpp"
#include "csdb/internal/types.hpp"
#include "csdb/pool.hpp"

#include "priv_crypto.hpp"

namespace csdb {

SHARED_DATA_CLASS_IMPLEMENTATION(TransactionID)

TransactionID::TransactionID(PoolHash poolHash, sequence_t index)
: d(new priv(poolHash, index)) {
}

bool TransactionID::is_valid() const noexcept {
  return !d->pool_hash_.is_empty();
}

PoolHash TransactionID::pool_hash() const noexcept {
  return d->pool_hash_;
}

TransactionID::sequence_t TransactionID::index() const noexcept {
  return d->index_;
}

std::string TransactionID::to_string() const noexcept {
  std::ostringstream os;
  os << d->pool_hash_.to_string() << ':' << std::hex << std::setfill('0') << std::setw(8) << d->index_;
  return os.str();
}

TransactionID TransactionID::from_string(const ::std::string& str) {
  TransactionID res;
  auto pos = str.find(':');
  if (::std::string::npos != pos) {
    PoolHash ph = PoolHash::from_string(::std::string(str, 0, pos));
    if (!ph.is_empty()) {
      const char* start = str.c_str() + pos + 1;
      if ('\0' != (*start)) {
        char* end = nullptr;
        uintmax_t idx = strtoumax(start, &end, 10);
        if ((end != start) && ('\0' == (*end))) {
          res.d->pool_hash_ = ph;
          res.d->index_ = static_cast<sequence_t>(idx);
        }
      }
    }
  }
  return res;
}

bool TransactionID::operator==(const TransactionID& other) const noexcept {
  return pool_hash() == other.pool_hash() && index() == other.index();
}

bool TransactionID::operator!=(const TransactionID& other) const noexcept {
  return !operator==(other);
}

bool TransactionID::operator<(const TransactionID& other) const noexcept {
  if (pool_hash() == other.pool_hash())
    return index() < other.index();
  else
    return pool_hash() < other.pool_hash();
}

void TransactionID::put(::csdb::priv::obstream& os) const {
  os.put(d->pool_hash_);
  os.put(d->index_);
}

bool TransactionID::get(::csdb::priv::ibstream& is) {
  return is.get(d->pool_hash_) && is.get(d->index_);
}

SHARED_DATA_CLASS_IMPLEMENTATION(Transaction)

Transaction::Transaction(int64_t innerID, Address source, Address target, Currency currency, Amount amount,
                         AmountCommission max_fee, AmountCommission counted_fee, std::string signature)
: d(new priv(innerID, source, target, currency, amount, max_fee, counted_fee, signature)) {
}

bool Transaction::is_valid() const noexcept {
  const priv* data = d.constData();
  return data->source_.is_valid() && data->target_.is_valid() && data->currency_.is_valid() && (data->amount_ >= 0_c) &&
         (data->source_ != data->target_ || data->user_fields_.size() == 3); // user_fields_count == 3 from the smartcontracts.hpp
}

bool Transaction::is_read_only() const noexcept {
  return d->read_only_;
}

TransactionID Transaction::id() const noexcept {
  return d->id_;
}

int64_t Transaction::innerID() const noexcept {
  return d->innerID_;
}

Address Transaction::source() const noexcept {
  return d->source_;
}

Address Transaction::target() const noexcept {
  return d->target_;
}

Currency Transaction::currency() const noexcept {
  return d->currency_;
}

Amount Transaction::amount() const noexcept {
  return d->amount_;
}

AmountCommission Transaction::max_fee() const noexcept {
  return d->max_fee_;
}

AmountCommission Transaction::counted_fee() const noexcept {
  return d->counted_fee_;
}

std::string Transaction::signature() const noexcept {
  return d->signature_;
}

void Transaction::set_innerID(int64_t innerID) {
  if (!d.constData()->read_only_) {
    d->innerID_ = innerID;
  }
}

void Transaction::set_source(Address source) {
  if (!d.constData()->read_only_) {
    d->source_ = source;
  }
}

void Transaction::set_target(Address target) {
  if (!d.constData()->read_only_) {
    d->target_ = target;
  }
}

void Transaction::set_currency(Currency currency) {
  if (!d.constData()->read_only_) {
    d->currency_ = currency;
  }
}

void Transaction::set_amount(Amount amount) {
  if (!d.constData()->read_only_) {
    d->amount_ = amount;
  }
}

void Transaction::set_max_fee(AmountCommission max_fee) {
  if (!d.constData()->read_only_) {
    d->max_fee_ = max_fee;
  }
}

void Transaction::set_counted_fee(AmountCommission counted_fee) {
  if (!d.constData()->read_only_) {
    d->counted_fee_ = counted_fee;
  }
}

void Transaction::set_counted_fee_unsafe(AmountCommission counted_fee) {
  if (!d.constData()->read_only_) {
    auto& constPrivShared = const_cast<const decltype(d)&>(d);
    const priv* constPrivPtr = constPrivShared.data();
    priv* privPtr = const_cast<priv*>(constPrivPtr);
    privPtr->counted_fee_ = counted_fee;
  }
}

void Transaction::set_signature(std::string signature) {
  if (!d.constData()->read_only_) {
    d->signature_ = signature;
  }
}

bool Transaction::add_user_field(user_field_id_t id, UserField field) noexcept {
  if (d.constData()->read_only_ || (!field.is_valid())) {
    return false;
  }
  d->user_fields_[id] = field;
  return true;
}

UserField Transaction::user_field(user_field_id_t id) const noexcept {
  const priv* data = d.constData();
  auto it = data->user_fields_.find(id);
  return (data->user_fields_.end() == it) ? UserField{} : it->second;
}

::std::set<user_field_id_t> Transaction::user_field_ids() const noexcept {
  ::std::set<user_field_id_t> res;
  const priv* data = d.constData();
  for (const auto& it : data->user_fields_) {
    res.insert(it.first);
  }
  return res;
}

cs::Bytes Transaction::to_binary() {
  if (!is_valid()) {
    return cs::Bytes();
  }
  ::csdb::priv::obstream os;
  put(os);
  return os.buffer();
}

Transaction Transaction::from_binary(const cs::Bytes data) {
  Transaction t;
  ::csdb::priv::ibstream is(data.data(), data.size());
  if (!t.get(is)) {
    return Transaction();
  }
  else {
    return t;
  }
}

Transaction Transaction::from_byte_stream(const char* data, size_t m_size) {
  Transaction t;
  ::csdb::priv::ibstream is(data, m_size);
  if (!t.get(is)) {
    return Transaction();
  }
  else {
    return t;
  }
}

std::vector<uint8_t> Transaction::to_byte_stream() const {
  ::csdb::priv::obstream os;
  put(os);
  return os.buffer();
}

bool Transaction::verify_signature(const cs::PublicKey& public_key) const {
  return cscrypto::VerifySignature(reinterpret_cast<const uint8_t*>(this->signature().data()),
    public_key.data(), this->to_byte_stream_for_sig().data(),
    this->to_byte_stream_for_sig().size());
}

std::vector<uint8_t> Transaction::to_byte_stream_for_sig() const {
  ::csdb::priv::obstream os;
  const priv* data = d.constData();
  uint8_t innerID[6];
  {
    auto ptr = reinterpret_cast<const uint8_t*>(&data->innerID_);
    std::copy(ptr, ptr + sizeof(innerID), innerID);  // only for little endian machines
  }
  innerID[5] |= ((data->source_.is_wallet_id() << 7) | (data->target_.is_wallet_id()) << 6);
  os.put(*reinterpret_cast<uint16_t*>(innerID));
  os.put(*reinterpret_cast<uint32_t*>(innerID + sizeof(uint16_t)));
  if (data->source_.is_wallet_id()) {
    os.put(data->source_.wallet_id());
  }
  else {
    os.put(data->source_.public_key());
  }
  if (data->target_.is_wallet_id()) {
    os.put(data->target_.wallet_id());
  }
  else {
    os.put(data->target_.public_key());
  }
  os.put(data->amount_);
  os.put(data->max_fee_);
  os.put(data->currency_);

  decltype(data->user_fields_) custom_user_fields(data->user_fields_.lower_bound(0), data->user_fields_.end());
  if (custom_user_fields.size()) {
    os.put_smart(custom_user_fields);
    auto buf = os.buffer();
    return buf;
  }
  else {
    uint8_t num_user_fields = 0;
    os.put(num_user_fields);
    return os.buffer();
  }
}

void Transaction::put(::csdb::priv::obstream& os) const {
  const priv* data = d.constData();
  uint8_t innerID[6];
  {
    auto ptr = reinterpret_cast<const uint8_t*>(&data->innerID_);
    std::copy(ptr, ptr + sizeof(innerID), innerID);  // only for little endian machines
  }
  innerID[5] |= ((data->source_.is_wallet_id() << 7) | (data->target_.is_wallet_id()) << 6);
  os.put(*reinterpret_cast<uint16_t*>(innerID));
  os.put(*reinterpret_cast<uint32_t*>(innerID + sizeof(uint16_t)));
  if (data->source_.is_wallet_id()) {
    os.put(data->source_.wallet_id());
  }
  else {
    os.put(data->source_.public_key());
  }
  if (data->target_.is_wallet_id()) {
    os.put(data->target_.wallet_id());
  }
  else {
    os.put(data->target_.public_key());
  }
  os.put(data->amount_);
  os.put(data->max_fee_);
  os.put(data->currency_);

  {
    uint8_t size = static_cast<uint8_t>(data->user_fields_.size());
    os.put(size);

    if (size) {
      os.put(data->user_fields_);
    }
  }

  os.put(data->signature_);
  os.put(data->counted_fee_);
}

bool Transaction::get(::csdb::priv::ibstream& is) {
  priv* data = d.data();
  bool res;

  {
    uint16_t lo = 0;
    uint32_t hi = 0;
    res = is.get(lo) && is.get(hi);

    if (!res) {
      return res;
    }

    data->innerID_ = (((uint64_t)hi & 0x3fffffff) << 16) | lo;

    if (hi & 0x80000000) {
      internal::WalletId id;
      res = is.get(id);

      if (!res) {
        return res;
      }

      data->source_ = Address::from_wallet_id(id);
    }
    else {
      cs::PublicKey key;
      res = is.get(key);

      if (!res) {
        return res;
      }

      data->source_ = Address::from_public_key(key);
    }

    if (hi & 0x40000000) {
      internal::WalletId id;
      res = is.get(id);

      if (!res) {
        return res;
      }

      data->target_ = Address::from_wallet_id(id);
    }
    else {
      cs::PublicKey key;
      res = is.get(key);

      if (!res) {
        return res;
      }

      data->target_ = Address::from_public_key(key);
    }
  }

  res = is.get(data->amount_);

  if (!res) {
    return res;
  }

  res = is.get(data->max_fee_);

  if (!res) {
    return res;
  }

  uint8_t parse;
  res = is.get(parse);

  if (!res) {
    return res;
  }

  data->currency_ = parse;
  res = is.get(parse);

  if (!res) {
    return res;
  }

  if (parse) {
    res = is.get(data->user_fields_);

    if (!res) {
      return res;
    }
  }

  return is.get(data->signature_) && is.get(data->counted_fee_);
}

void Transaction::set_time(const uint64_t ts) {
  d->time_ = ts;
}

uint64_t Transaction::get_time() const {
  return d->time_;
}

}  // namespace csdb
