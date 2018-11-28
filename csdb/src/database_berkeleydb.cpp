#include <db_cxx.h>
#include <cassert>
#include <cstdlib>
#include <exception>

#include <boost/filesystem.hpp>

#include "csdb/database_berkeleydb.hpp"
#include "csdb/pool.hpp"

#include "priv_crypto.hpp"

namespace csdb {

namespace {
template <typename T>
struct Dbt_copy : public Dbt {
  explicit Dbt_copy(const T &t)
  : t_copy(t) {
    init();
  }

  Dbt_copy() {
    init();
  }

  void init() {
    set_data(&t_copy);
    set_size(sizeof(T));
    set_ulen(sizeof(T));
    set_flags(DB_DBT_USERMEM);
  }

  explicit operator T() {
    return t_copy;
  }

private:
  T t_copy;
};

template <>
struct Dbt_copy<::csdb::internal::byte_array> : public Dbt {
  explicit Dbt_copy(const ::csdb::internal::byte_array &data) {
    set_data(const_cast<unsigned char *>(data.data()));
    set_size(data.size());
    set_ulen(data.size());
    set_flags(DB_DBT_USERMEM);
  }
};

struct Dbt_safe : public Dbt {
  Dbt_safe() {
    set_data(nullptr);
    set_flags(DB_DBT_MALLOC);
  }
  ~Dbt_safe() {
    void *buf = get_data();
    if (buf != nullptr) {
      free(buf);
    }
  }
};
}  // namespace

DatabaseBerkeleyDB::DatabaseBerkeleyDB()
: env_(static_cast<uint32_t>(0))
, db_blocks_(nullptr)
, db_seq_no_(nullptr) {
}

DatabaseBerkeleyDB::~DatabaseBerkeleyDB() {
  std::cout << "Attempt db_blocks_ to close...\n" << std::flush;
  db_blocks_->close(0);
  std::cout << "DB db_blocks_ was closed.\n" << std::flush;
  std::cout << "Attempt db_seq_no_ to close...\n" << std::flush;
  db_seq_no_->close(0);
  std::cout << "DB db_seq_no_ was closed.\n" << std::flush;
}

void DatabaseBerkeleyDB::set_last_error_from_berkeleydb(int status) {
  Error err = UnknownError;
  if (status == 0) {
    err = NoError;
  }
  else if (status == ENOENT) {
    err = NotFound;
  }
  if (NoError == err) {
    set_last_error(err);
  }
  else {
    set_last_error(err, "LevelDB error: %d", status);
  }
}

bool DatabaseBerkeleyDB::open(const std::string &path) {
  boost::filesystem::path direc(path);
  if (boost::filesystem::exists(direc)) {
    if (!boost::filesystem::is_directory(direc)) {
      return false;
    }
  }
  else {
    if (!boost::filesystem::create_directories(direc)) {
      return false;
    }
  }

  db_blocks_.reset(nullptr);
  db_seq_no_.reset(nullptr);

  DbEnv env(static_cast<uint32_t>(0));
  uint32_t db_env_open_flags =
      DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_RECOVER | DB_USE_ENVIRON | DB_PRIVATE | DB_INIT_LOG;
  try {
    //    env.open(path.c_str(), db_env_open_flags, 0);
    //    env.close(0); // this recover method does not work
  }
  catch (DbException &e) {
    std::cerr << "Error opening database environment: " << path << " and database "
              << "blockchain.db" << std::endl;
    std::cerr << e.what() << std::endl;
    return false;
  }
  catch (std::exception &e) {
    std::cerr << "Error opening database environment: " << path << " and database "
              << "blockchain.db" << std::endl;
    std::cerr << e.what() << std::endl;
    return false;
  }

  db_env_open_flags = DB_CREATE | DB_INIT_MPOOL | DB_INIT_CDB | DB_THREAD;
  env_.open(path.c_str(), db_env_open_flags, 0);

  auto db_blocks = std::make_unique<Db>(&env_, 0);
  auto db_seq_no = std::make_unique<Db>(&env_, 0);

  int status = db_blocks->open(nullptr, "blockchain.db", nullptr, DB_RECNO, DB_CREATE, 0);
  if (status != 0) {
    set_last_error_from_berkeleydb(status);
    return false;
  }
  db_blocks_.reset(db_blocks.release());

  status = db_seq_no->open(nullptr, "sequence.db", nullptr, DB_HASH, DB_CREATE, 0);
  if (status != 0) {
    set_last_error_from_berkeleydb(status);
    return false;
  }
  db_seq_no_.reset(db_seq_no.release());

  set_last_error();
  return true;
}

bool DatabaseBerkeleyDB::is_open() const {
  return static_cast<bool>(db_blocks_);
}

bool DatabaseBerkeleyDB::put(const byte_array &key, uint32_t seq_no, const byte_array &value) {
  if (!db_blocks_) {
    set_last_error(NotOpen);
    return false;
  }

  Dbt_copy<uint32_t> db_seq_no(seq_no + 1);
  Dbt_copy<byte_array> db_value(value);

  int status = db_blocks_->put(nullptr, &db_seq_no, &db_value, 0);
  if (status != 0) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  Dbt_copy<byte_array> db_key(key);
  status = db_seq_no_->put(nullptr, &db_key, &db_seq_no, 0);
  if (status != 0) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  set_last_error();
  return true;
}

bool DatabaseBerkeleyDB::get(const byte_array &key, byte_array *value) {
  if (!db_blocks_) {
    set_last_error(NotOpen);
    return false;
  }

  Dbt_copy<byte_array> db_key(key);
  if (value == nullptr) {
    return db_seq_no_->exists(nullptr, &db_key, 0) == 0;
  }

  Dbt_copy<uint32_t> db_seq_no;
  int status = db_seq_no_->get(nullptr, &db_key, &db_seq_no, 0);
  if (status != 0) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  Dbt_safe db_value;

  status = db_blocks_->get(nullptr, &db_seq_no, &db_value, 0);
  if (status != 0) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  auto begin = static_cast<uint8_t *>(db_value.get_data());
  value->assign(begin, begin + db_value.get_size());
  set_last_error();
  return true;
}

bool DatabaseBerkeleyDB::get(const uint32_t seq_no, byte_array *value) {
  if (!db_blocks_) {
    set_last_error(NotOpen);
    return false;
  }

  if (value == nullptr) {
    return false;
  }

  Dbt_safe db_value;
  Dbt_copy<uint32_t> db_seq_no(seq_no);

  int status = db_blocks_->get(nullptr, &db_seq_no, &db_value, 0);
  if (status != 0) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  auto begin = static_cast<uint8_t *>(db_value.get_data());
  value->assign(begin, begin + db_value.get_size());
  set_last_error();
  return true;
}

bool DatabaseBerkeleyDB::remove(const byte_array& key) {
  if (!db_blocks_) {
    set_last_error(NotOpen);
    return false;
  }

  Dbt_copy<byte_array> db_key(key);
  Dbt_copy<uint32_t> db_seq_no;
  int status = db_seq_no_->get(nullptr, &db_key, &db_seq_no, 0);
  if (status != 0) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  status = db_seq_no_->del(nullptr, &db_key, 0);
  if (status != 0) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  status = db_blocks_->del(nullptr, &db_seq_no, 0);
  if (status != 0) {
    set_last_error_from_berkeleydb(status);
    return false;
  }

  set_last_error();
  return true;
}

bool DatabaseBerkeleyDB::write_batch(const ItemList&) {
  assert(false);

  if (!db_blocks_) {
    set_last_error(NotOpen);
    return false;
  }

  set_last_error();
  return true;
}

class DatabaseBerkeleyDB::Iterator final : public Database::Iterator {
public:
  explicit Iterator(Dbc *it)
  : it_(it)
  , valid_(false) {
    if (it != nullptr) {
      valid_ = true;
    }
  }
  ~Iterator() final {
    if (it_ != nullptr) {
      it_->close();
    }
  }
  bool is_valid() const final {
    return valid_;
  }

  void seek_to_first() final {
    if (it_ == nullptr) {
      return;
    }

    Dbt key;
    Dbt_safe value;

    int ret = it_->get(&key, &value, DB_FIRST);
    if (ret == 0) {
      set_value(value);
      valid_ = true;
    }
    else {
      valid_ = false;
    }
  }

  void seek_to_last() final {
    assert(false);
  }

  void seek(const ::csdb::internal::byte_array&) final {
    assert(false);
  }

  void next() override final {
    if (it_ == nullptr) {
      return;
    }

    Dbt key;
    Dbt_safe value;

    int ret = it_->get(&key, &value, DB_NEXT);
    if (ret == 0) {
      set_value(value);
      valid_ = true;
    }
    else {
      valid_ = false;
    }
  }

  void prev() final {
    assert(false);
  }

  ::csdb::internal::byte_array key() const final {
    return ::csdb::internal::byte_array{};
  }

  ::csdb::internal::byte_array value() const final {
    if (valid_) {
      return value_;
    }
    return ::csdb::internal::byte_array{};
  }

private:
  void set_value(const Dbt& value) {
    auto begin = static_cast<uint8_t *>(value.get_data());
    value_.assign(begin, begin + value.get_size());
  }

  Dbc *it_;
  bool valid_;
  internal::byte_array value_;
};

DatabaseBerkeleyDB::IteratorPtr DatabaseBerkeleyDB::new_iterator() {
  if (!db_blocks_) {
    set_last_error(NotOpen);
    return nullptr;
  }

  Dbc *cursorp;
  db_blocks_->cursor(nullptr, &cursorp, 0);

  return Database::IteratorPtr(new DatabaseBerkeleyDB::Iterator(cursorp));
}

}  // namespace csdb
