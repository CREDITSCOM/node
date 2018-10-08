#include <cstdlib>
#include <cassert>
#include <exception>
#include <db_cxx.h>

#include <boost/filesystem.hpp>

#include "csdb/database_berkeleydb.h"
#include "csdb/pool.h"

#include "priv_crypto.h"

namespace csdb {

namespace {
template<typename T>
struct Dbt_copy: public Dbt
{
  Dbt_copy(const T &t) :
    t_copy(t)
  {
    init();
  }

  Dbt_copy()
  {
    init();
  }

  void init()
  {
    set_data(&t_copy);
    set_size(sizeof(T));
    set_ulen(sizeof(T));
    set_flags(DB_DBT_USERMEM);
  }

  operator T()
  {
    return t_copy;
  }
private:
  T t_copy;
};

template<>
struct Dbt_copy<::csdb::internal::byte_array>: public Dbt
{
  Dbt_copy(const ::csdb::internal::byte_array &data)
  {
    set_data(const_cast<unsigned char *>(data.data()));
    set_size(data.size());
    set_ulen(data.size());
    set_flags(DB_DBT_USERMEM);
  }
};

struct Dbt_safe : public Dbt
{
  Dbt_safe()
  {
    set_data(NULL);
    set_flags(DB_DBT_MALLOC);
  }
  ~Dbt_safe()
  {
    void* buf = get_data();
    if (buf != NULL) {
      free(buf);

    }
  }
};
} // namespace

DatabaseBerkeleyDB::DatabaseBerkeleyDB() :
  env_(static_cast<uint32_t>(0)),
  db_blocks_(nullptr),
  db_seq_no_(nullptr)
{
}

DatabaseBerkeleyDB::~DatabaseBerkeleyDB()
{
  db_blocks_->close(0);
  db_seq_no_->close(0);
}

void DatabaseBerkeleyDB::set_last_error_from_berkeleydb(int status)
{
  Error err = UnknownError;
  if (!status) {
    err = NoError;
  } else if (status == ENOENT) {
    err = NotFound;
  }
  if (NoError == err) {
    set_last_error(err);
  } else {
    set_last_error(err, "LevelDB error: %d", status);
  }
}

bool DatabaseBerkeleyDB::open(const std::string& path)
{
  boost::filesystem::path direc(path);
  if (boost::filesystem::exists(direc)) {
    if (!boost::filesystem::is_directory(direc))
      return false;
  } else {
    if (!boost::filesystem::create_directories(direc))
      return false;
  }

  db_blocks_.reset(nullptr);
  db_seq_no_.reset(nullptr);

  uint32_t db_env_open_flags = DB_CREATE | DB_INIT_MPOOL | DB_INIT_CDB | DB_THREAD;
  env_.open(path.c_str(), db_env_open_flags, 0);

  auto db_blocks = new Db(&env_, 0);
  auto db_seq_no = new Db(&env_, 0);

  int status = db_blocks->open(NULL, "blockchain.db", NULL, DB_RECNO, DB_CREATE, 0);
  if (status) {
    set_last_error_from_berkeleydb(status);
    return false;
  }
  db_blocks_.reset(db_blocks);

  status = db_seq_no->open(NULL, "sequence.db", NULL, DB_HASH, DB_CREATE, 0);
  if (status) {
    set_last_error_from_berkeleydb(status);
    return false;
  }
  db_seq_no_.reset(db_seq_no);

  set_last_error();
  return true;
}

bool DatabaseBerkeleyDB::is_open() const
{
  return static_cast<bool>(db_blocks_);
}

bool DatabaseBerkeleyDB::put(const byte_array &key, uint32_t seq_no, const byte_array &value)
{
  if (!db_blocks_) {
    set_last_error(NotOpen);
    return false;
  }

  Dbt_copy<uint32_t> db_seq_no(seq_no + 1);
  Dbt_copy<byte_array> db_value(value);

  int status = db_blocks_->put(nullptr, &db_seq_no, &db_value, 0);
  if (status) {
    set_last_error_from_berkeleydb(status);
    return false;
  }


  Dbt_copy<byte_array> db_key(key);
  status = db_seq_no_->put(nullptr, &db_key, &db_seq_no, 0);
  if (status) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  set_last_error();
  return true;
}

bool DatabaseBerkeleyDB::get(const byte_array &key, byte_array *value)
{
  if (!db_blocks_) {
    set_last_error(NotOpen);
    return false;
  }

  Dbt_copy<byte_array> db_key(key);
  if (value == nullptr) {
    if (db_seq_no_->exists(nullptr, &db_key, 0) == 0) {
      return true;
    } else {
      return false;
    }
  }

  Dbt_copy<uint32_t> db_seq_no;
  int status = db_seq_no_->get(nullptr, &db_key, &db_seq_no, 0);
  if (status) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  Dbt_safe db_value;

  status = db_blocks_->get(nullptr, &db_seq_no, &db_value, 0);
  if (status) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  auto begin = reinterpret_cast<uint8_t *>(db_value.get_data());
  value->assign(begin, begin + db_value.get_size());
  set_last_error();
  return true;
}

bool DatabaseBerkeleyDB::remove(const byte_array &key)
{
  assert(false);

  if (!db_blocks_) {
    set_last_error(NotOpen);
    return false;
  }

  set_last_error();
  return true;
}

bool DatabaseBerkeleyDB::write_batch(const ItemList &items)
{
  assert(false);

  if (!db_blocks_) {
    set_last_error(NotOpen);
    return false;
  }

  set_last_error();
  return true;
}

class DatabaseBerkeleyDB::Iterator final : public Database::Iterator
{
public:
  Iterator(Dbc *it) :
    it_(it),
    valid_(false)
  {
    if (it != nullptr) {
      valid_ = true;
    }
  }
  ~Iterator()
  {
    if (it_ != nullptr) {
      it_->close();
    }
  }
  bool is_valid() const override final
  {
    return valid_;
  }

  void seek_to_first() override final
  {
    if (!it_) return;

    Dbt key;
    Dbt_safe value;

    int ret = it_->get(&key, &value, DB_FIRST);
    if (!ret) {
      set_value(value);
      valid_ = true;
    } else {
      valid_ = false;
    }
  }

  void seek_to_last() override final
  {
    assert(false);
  }

  void seek(const ::csdb::internal::byte_array &key) override final
  {
    assert(false);
  }

  void next() override final
  {
    if (!it_) return;

    Dbt key;
    Dbt_safe value;

    int ret = it_->get(&key, &value, DB_NEXT);
    if (!ret) {
      set_value(value);
      valid_ = true;
    } else {
      valid_ = false;
    }
  }

  void prev() override final
  {
    assert(false);
  }

  ::csdb::internal::byte_array key() const override final
  {
    return ::csdb::internal::byte_array{};
  }

  ::csdb::internal::byte_array value() const override final
  {
    if (valid_) {
      return value_;
    } else {
      return ::csdb::internal::byte_array{};
    }
  }

private:

  void set_value(Dbt &value) {
    auto begin = reinterpret_cast<uint8_t *>(value.get_data());
    value_.assign(begin, begin + value.get_size());
  }

  Dbc *it_;
  bool valid_;
  internal::byte_array value_;
};

DatabaseBerkeleyDB::IteratorPtr DatabaseBerkeleyDB::new_iterator()
{
  if (!db_blocks_) {
    set_last_error(NotOpen);
    return nullptr;
  }

  Dbc *cursorp;
  db_blocks_->cursor(nullptr, &cursorp, 0);

  return Database::IteratorPtr(new DatabaseBerkeleyDB::Iterator(cursorp));
}

} // namespace csdb
