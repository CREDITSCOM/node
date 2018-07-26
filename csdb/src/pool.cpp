#include "csdb/pool.h"

#include <sstream>
#include <iomanip>
#include <map>
#include <algorithm>

#include "csdb/csdb.h"

#include "csdb/internal/shared_data_ptr_implementation.h"
#include "csdb/internal/utils.h"
#include "binary_streams.h"
#include "priv_crypto.h"
#include "transaction_p.h"

namespace csdb {

class PoolHash::priv : public ::csdb::internal::shared_data
{
public:
  internal::byte_array value;
};
SHARED_DATA_CLASS_IMPLEMENTATION(PoolHash)

bool PoolHash::is_empty() const noexcept
{
  return d->value.empty();
}

size_t PoolHash::size()  const noexcept
{
  return d->value.size();
}

std::string PoolHash::to_string() const noexcept
{
  return internal::to_hex(d->value);
}

::csdb::internal::byte_array PoolHash::to_binary() const noexcept
{
  return d->value;
}

PoolHash PoolHash::from_binary(const ::csdb::internal::byte_array& data)
{
  const size_t sz = data.size();
  PoolHash res;
  if ((0 == sz)
      || (::csdb::priv::crypto::hash_size == sz)
      ) {
    res.d->value = data;
  }
  return res;
}

bool PoolHash::operator ==(const PoolHash &other) const noexcept
{
  return (d == other.d) || (d->value == other.d->value);
}

bool PoolHash::operator <(const PoolHash &other) const noexcept
{
  return (d != other.d) && (d->value < other.d->value);
}

PoolHash PoolHash::from_string(const ::std::string& str)
{
  const ::csdb::internal::byte_array hash = ::csdb::internal::from_hex(str);
  const size_t sz = hash.size();
  PoolHash res;
  if ((0 == sz)
      || (::csdb::priv::crypto::hash_size == sz)
      ) {
    res.d->value = hash;
  }
  return res;
}

PoolHash PoolHash::calc_from_data(const internal::byte_array &data)
{
  PoolHash res;
  res.d->value = ::csdb::priv::crypto::calc_hash(data);
  return res;
}

void PoolHash::put(::csdb::priv::obstream &os) const
{
  os.put(d->value);
}

bool PoolHash::get(::csdb::priv::ibstream &is)
{
  return is.get(d->value);
}

class Pool::priv : public ::csdb::internal::shared_data
{
  priv() : is_valid_(false), read_only_(false), sequence_(0) {}
  priv(PoolHash previous_hash, Pool::sequence_t sequence, ::csdb::Storage::WeakPtr storage) :
    is_valid_(true),
    read_only_(false),
    previous_hash_(previous_hash),
    sequence_(sequence),
    storage_(storage)
  {}

  void put(::csdb::priv::obstream& os) const
  {
    os.put(previous_hash_);
    os.put(sequence_);

    os.put(transactions_.size());
    for(const auto& it : transactions_) {
      os.put(it);
    }

    os.put(user_fields_);
  }

  bool get_meta(::csdb::priv::ibstream& is, size_t& cnt) {
	  if (!is.get(previous_hash_)) {
		  return false;
	  }

	  if (!is.get(sequence_))
		  return false;

	  if (!is.get(cnt)) {
		  return false;
	  }

	  return true;
  }

  bool get(::csdb::priv::ibstream& is)
  {
	size_t cnt;
	if (!get_meta(is, cnt))
		return false;

    transactions_.clear();
    transactions_.reserve(cnt);
    for(size_t i = 0; i < cnt; ++i )
    {
      Transaction tran;
      if(!is.get(tran))
        return false;
      transactions_.emplace_back(tran);
    }

    if(!is.get(user_fields_)) {
      return false;
    }

    is_valid_ = true;
    return true;
  }

  void compose()
  {
    if (!is_valid_) {
      binary_representation_.clear();
      hash_ = PoolHash();
      return;
    }

    /*if (!binary_representation_.empty()) {
      return;
    }*/

    ::csdb::priv::obstream os;
    put(os);
    binary_representation_ = os.buffer();

    update_transactions();
  }

  void update_transactions()
  {
    read_only_ = true;
    hash_ = PoolHash::calc_from_data(binary_representation_);
    for (size_t idx = 0; idx < transactions_.size(); ++idx) {
      transactions_[idx].d->_update_id(hash_, idx);
    }
  }

  Storage get_storage(Storage candidate)
  {
    if (candidate.isOpen()) {
      return candidate;
    }

    candidate = Storage(storage_);
    if (candidate.isOpen()) {
      return candidate;
    }

    return ::csdb::defaultStorage();
  }

  bool is_valid_;
  bool read_only_;
  PoolHash hash_;
  PoolHash previous_hash_;
  Pool::sequence_t sequence_;
  std::vector<Transaction> transactions_;
  ::std::map<::csdb::user_field_id_t, ::csdb::UserField> user_fields_;
  ::csdb::internal::byte_array binary_representation_;
  ::csdb::Storage::WeakPtr storage_;
  friend class Pool;
};
SHARED_DATA_CLASS_IMPLEMENTATION(Pool)

Pool::Pool(PoolHash previous_hash, sequence_t sequence, Storage storage) :
  d(new priv(previous_hash, sequence, storage.weak_ptr()))
{
}

bool Pool::is_valid() const noexcept
{
  return d->is_valid_;
}

bool Pool::is_read_only() const noexcept
{
  return d->read_only_;
}

PoolHash Pool::hash() const noexcept
{
  return d->hash_;
}

PoolHash Pool::previous_hash() const noexcept
{
  return d->previous_hash_;
}

Storage Pool::storage() const noexcept
{
  return Storage(d->storage_);
}

Transaction Pool::transaction(size_t index) const
{
  return (d->transactions_.size() > index) ? d->transactions_[index] : Transaction{};
}

Transaction Pool::transaction(TransactionID id) const
{
  if ((!d->is_valid_) || (!d->read_only_)
      || (!id.is_valid()) || (id.pool_hash() != d->hash_)
      || (d->transactions_.size() <= id.d->index_)) {
    return Transaction{};
  }
  return d->transactions_[id.d->index_];
}

Transaction Pool::get_last_by_source(Address source) const noexcept
{
  const auto data = d.constData();

  if ((!data->is_valid_))
  {
    return Transaction{};
  }

  auto it_rend = data->transactions_.rend();
  for (auto it = data->transactions_.rbegin(); it != it_rend; ++it)
  {
    const auto& t = *it;

    if (t.source() == source)
    {
      return t;
    }
  }

  return Transaction{};
}

Transaction Pool::get_last_by_target(Address target) const noexcept
{
  const auto data = d.constData();

  if ((!data->is_valid_))
  {
    return Transaction{};
  }

  auto it_rend = data->transactions_.rend();
  for (auto it = data->transactions_.rbegin(); it != it_rend; ++it)
  {
    const auto t = *it;

    if (t.target() == target)
    {
      return t;
    }
  }

  return Transaction{};
}

bool Pool::add_transaction(Transaction transaction
#ifdef CSDB_UNIT_TEST
                     , bool skip_check
#endif
                     )
{
  if(d.constData()->read_only_) {
    return false;
  }

  if (!transaction.is_valid()) {
    return false;
  }

#ifdef CSDB_UNIT_TEST
  if (!skip_check) {
#endif
  /// \todo Add transaction checking.
#ifdef CSDB_UNIT_TEST
  }
#endif

  d->transactions_.push_back(Transaction(new Transaction::priv(*(transaction.d.constData()))));
  return true;
}

size_t Pool::transactions_count() const noexcept
{
  return d->transactions_.size();
}

Pool::sequence_t Pool::sequence() const noexcept
{
  return d->sequence_;
}

void Pool::set_sequence(Pool::sequence_t seq) noexcept
{
  if (d.constData()->read_only_) {
    return;
  }

  priv* data = d.data();
  data->is_valid_ = true;
  data->sequence_ = seq;
}

void Pool::set_previous_hash(PoolHash previous_hash) noexcept
{
  if (d.constData()->read_only_) {
    return;
  }

  priv* data = d.data();
  data->is_valid_ = true;
  data->previous_hash_ = previous_hash;
}

void Pool::set_storage(Storage storage) noexcept
{
  // We can set up storage even if Pool is read-only
  priv* data = d.data();
  data->is_valid_ = true;
  data->storage_ = storage.weak_ptr();
}

std::vector<csdb::Transaction>& Pool::transactions()
{
	return d->transactions_;
}

  bool Pool::add_user_field(user_field_id_t id, UserField field) noexcept
{
  if (d.constData()->read_only_ || (!field.is_valid())) {
    return false;
  }

  priv* data = d.data();
  data->is_valid_ = true;
  data->user_fields_[id] = field;

  return true;
}

UserField Pool::user_field(user_field_id_t id) const noexcept
{
  const priv* data = d.constData();
  auto it = data->user_fields_.find(id);
  return (data->user_fields_.end() == it) ? UserField{} : it->second;
}

::std::set<user_field_id_t> Pool::user_field_ids() const noexcept
{
  ::std::set<user_field_id_t> res;
  const priv* data = d.constData();
  for (const auto& it : data->user_fields_) {
    res.insert(it.first);
  }
  return res;
}

bool Pool::compose()
{
  if (d.constData()->read_only_) {
    return true;
  }

  if (!d.constData()->is_valid_) {
    return false;
  }

  d->compose();
  return true;
}

::csdb::internal::byte_array Pool::to_binary() const noexcept
{
  return d->binary_representation_;
}

Pool Pool::from_binary(const ::csdb::internal::byte_array& data)
{
	priv *p = new priv();
	::csdb::priv::ibstream is(data.data(), data.size());
	if (!p->get(is)) {
		delete p;
		return Pool();
	}
	p->binary_representation_ = data;
	p->update_transactions();
	return Pool(p);
}

Pool Pool::meta_from_binary(const ::csdb::internal::byte_array& data, size_t& cnt)
{
	priv *p = new priv();
	::csdb::priv::ibstream is(data.data(), data.size());

	if (!p->get_meta(is, cnt)) {
		delete p;
		return Pool();
	}

	p->binary_representation_ = data;
	return Pool(p);
}

  Pool Pool::from_byte_stream(const char* data, size_t size) {
    priv *p = new priv();
    ::csdb::priv::ibstream is(data, size);

    if (!p->get(is)) {
      delete p;
      return Pool();
    }

    return Pool(p);
  }

  char* Pool::to_byte_stream(size_t& size) {
	  if (d->binary_representation_.empty()) {
		  ::csdb::priv::obstream os;
		  d->put(os);
		  d->binary_representation_ = std::move(const_cast<std::vector<uint8_t>&>(os.buffer()));
	  }

	  size = d->binary_representation_.size();
	  return (char*)(d->binary_representation_.data());
  }

  bool Pool::save(Storage storage)
{
  if ((!d.constData()->is_valid_) || ((!d.constData()->read_only_))) {
    return false;
  }

  Storage s = d->get_storage(storage);
  if (!s.isOpen()) {
    return false;
  }

  if (s.pool_save(*this)) {
    d->storage_ = s.weak_ptr();
    return true;
  }

  return false;
}

Pool Pool::load(PoolHash hash, Storage storage)
{
  if (!storage.isOpen()) {
    storage = ::csdb::defaultStorage();
  }

  Pool res = storage.pool_load(hash);
  if (res.is_valid()) {
    res.set_storage(storage);
  }
  return res;
}

  bool Pool::clear() noexcept
  {
    d->transactions_.clear();

    if (!d->transactions_.empty())
      return false;
    return true;
  }
} // namespace csdb
