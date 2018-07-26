#include "csdb/transaction.h"
#include "transaction_p.h"

#include <sstream>
#include <iomanip>
#include <cinttypes>

#include "csdb/currency.h"
#include "csdb/address.h"
#include "csdb/amount.h"
#include "csdb/pool.h"
#include "binary_streams.h"

namespace csdb {

SHARED_DATA_CLASS_IMPLEMENTATION(TransactionID)

TransactionID::TransactionID(PoolHash poolHash, sequence_t index) :
  d(new priv(poolHash, index))
{
}

bool TransactionID::is_valid() const noexcept
{
  return !d->pool_hash_.is_empty();
}

PoolHash TransactionID::pool_hash() const noexcept
{
  return d->pool_hash_;
}

TransactionID::sequence_t TransactionID::index() const noexcept
{
  return d->index_;
}

std::string TransactionID::to_string() const noexcept
{
  std::ostringstream os;
  os << d->pool_hash_.to_string() << ':' << std::hex << std::setfill('0') << std::setw(8) << d->index_;
  return os.str();
}

TransactionID TransactionID::from_string(const ::std::string& str)
{
  TransactionID res;
  auto pos = str.find(':');
  if (::std::string::npos != pos) {
    PoolHash ph = PoolHash::from_string(::std::string(str, 0, pos));
    if (!ph.is_empty()) {
      const char *start = str.c_str() + pos + 1;
      if ('\0' != (*start)) {
        char *end = nullptr;
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

bool TransactionID::operator ==(const TransactionID &other) const noexcept
{
  return pool_hash() == other.pool_hash() && index() == other.index();
}

bool TransactionID::operator !=(const TransactionID &other) const noexcept
{
  return !operator==(other);
}

bool TransactionID::operator <(const TransactionID &other) const noexcept
{
  if(pool_hash() == other.pool_hash())
    return index() < other.index();
  else
    return pool_hash() < other.pool_hash();
}

void TransactionID::put(::csdb::priv::obstream &os) const
{
  os.put(d->pool_hash_);
  os.put(d->index_);
}

bool TransactionID::get(::csdb::priv::ibstream &is)
{
  return is.get(d->pool_hash_) && is.get(d->index_);
}

SHARED_DATA_CLASS_IMPLEMENTATION(Transaction)

Transaction::Transaction(Address source, Address target, Currency currency, Amount amount) :
  d(new priv(source, target, currency, amount, amount))
{
}

Transaction::Transaction(Address source, Address target, Currency currency, Amount amount, Amount balance) :
  d(new priv(source, target, currency, amount, balance))
{
}

bool Transaction::is_valid() const noexcept
{
  const priv* data = d.constData();
  return data->source_.is_valid()
      && data->target_.is_valid()
      && data->currency_.is_valid()
      && (data->amount_ > 0_c)
      && (data->source_ != data->target_);
}

bool Transaction::is_read_only() const noexcept
{
  return d->read_only_;
}

TransactionID Transaction::id() const noexcept
{
  return d->id_;
}

Address Transaction::source() const noexcept
{
  return d->source_;
}

Address Transaction::target() const noexcept
{
  return d->target_;
}

Currency Transaction::currency() const noexcept
{
  return d->currency_;
}

Amount Transaction::amount() const noexcept
{
  return d->amount_;
}

Amount Transaction::balance() const noexcept
{
  return d->balance_;
}

void Transaction::set_source(Address source)
{
  if (!d.constData()->read_only_) {
    d->source_ = source;
  }
}

void Transaction::set_target(Address target)
{
  if (!d.constData()->read_only_) {
    d->target_ = target;
  }
}

void Transaction::set_currency(Currency currency)
{
  if (!d.constData()->read_only_) {
    d->currency_ = currency;
  }
}

void Transaction::set_amount(Amount amount)
{
  if (!d.constData()->read_only_) {
    d->amount_ = amount;
  }
}

void Transaction::set_balance(Amount balance)
{
  if (!d.constData()->read_only_) {
    d->balance_ = balance;
  }
}

bool Transaction::add_user_field(user_field_id_t id, UserField field) noexcept
{
  if (d.constData()->read_only_ || (!field.is_valid())) {
    return false;
  }
  d->user_fields_[id] = field;
  return true;
}

UserField Transaction::user_field(user_field_id_t id) const noexcept
{
  const priv* data = d.constData();
  auto it = data->user_fields_.find(id);
  return (data->user_fields_.end() == it) ? UserField{} : it->second;
}

::std::set<user_field_id_t> Transaction::user_field_ids() const noexcept
{
  ::std::set<user_field_id_t> res;
  const priv* data = d.constData();
  for (const auto& it : data->user_fields_) {
    res.insert(it.first);
  }
  return res;
}

::csdb::internal::byte_array Transaction::to_binary()
{
  if (!is_valid()) {
    return ::csdb::internal::byte_array();
  }
  ::csdb::priv::obstream os;
  put(os);
  return os.buffer();
}

Transaction Transaction::from_binary(const ::csdb::internal::byte_array data)
{
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


void Transaction::put(::csdb::priv::obstream &os) const
{
  const priv* data = d.constData();
  os.put(data->source_);
  os.put(data->target_);
  os.put(data->currency_);
  os.put(data->amount_);
  os.put(data->balance_);
  os.put(data->user_fields_);
}

bool Transaction::get(::csdb::priv::ibstream &is)
{
  priv* data = d.data();
  return is.get(data->source_)
      && is.get(data->target_)
      && is.get(data->currency_)
      && is.get(data->amount_)
      && is.get(data->balance_)
      && is.get(data->user_fields_);
}

} // namespace csdb
