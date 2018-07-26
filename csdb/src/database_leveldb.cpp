#include "csdb/database_leveldb.h"

#include "leveldb/db.h"
#include "leveldb/cache.h"
#include "leveldb/status.h"
#include "leveldb/write_batch.h"

namespace csdb {

namespace {
::leveldb::Slice slice(const ::csdb::internal::byte_array& data)
{
  return ::leveldb::Slice(static_cast<const char*>(static_cast<const void*>(data.data())), data.size());
}

::csdb::internal::byte_array to_byte_array(const ::leveldb::Slice& data)
{
  ::csdb::internal::byte_array::const_pointer d
      = static_cast<::csdb::internal::byte_array::const_pointer>(static_cast<const void*>(data.data()));
  return ::csdb::internal::byte_array(d, d + data.size());
}

} // namespace

DatabaseLevelDB::DatabaseLevelDB()
{
}

DatabaseLevelDB::~DatabaseLevelDB()
{
}

void DatabaseLevelDB::set_last_error_from_leveldb(const ::leveldb::Status& status)
{
  Error err = UnknownError;
  if (status.ok()) {
    err = NoError;
  } else if (status.IsNotFound()) {
    err = NotFound;
  } else if (status.IsCorruption()) {
    err = Corruption;
  } else if (status.IsNotSupportedError()) {
    err = NotSupported;
  } else if (status.IsInvalidArgument()) {
    err = InvalidArgument;
  } else if (status.IsIOError()) {
    err = IOError;
  }
  if (NoError == err) {
    set_last_error(err);
  } else {
    set_last_error(err, "LevelDB error: %s", status.ToString().c_str());
  }
}

bool DatabaseLevelDB::open(const std::string& path, const leveldb::Options& options)
{
  db_.reset(nullptr);
  leveldb::DB* db = nullptr;
  leveldb::Status status = leveldb::DB::Open(options, path, &db);
  if (!status.ok()) {
    set_last_error_from_leveldb(status);
    return false;
  }
  db_.reset(db);
  set_last_error();
  return true;
}

bool DatabaseLevelDB::open(const std::string& path)
{
  ::leveldb::Options options;
  options.block_cache = leveldb::NewLRUCache(1024 * 1024 * 1024);
  options.create_if_missing = true;
  return open(path, options);
}

bool DatabaseLevelDB::is_open() const
{
  return static_cast<bool>(db_);
}

bool DatabaseLevelDB::put(const byte_array &key, const byte_array &value)
{
  if (!db_) {
    set_last_error(NotOpen);
    return false;
  }

  leveldb::Status status = db_->Put(leveldb::WriteOptions(), slice(key), slice(value));
  if (!status.ok()) {
    set_last_error_from_leveldb(status);
    return false;
  }
  set_last_error();
  return true;
}

bool DatabaseLevelDB::get(const byte_array &key, byte_array *value)
{
  if (!db_) {
    set_last_error(NotOpen);
    return false;
  }

  std::string result;
  leveldb::Status status = db_->Get(leveldb::ReadOptions(), slice(key), &result);
  if (!status.ok()) {
    set_last_error_from_leveldb(status);
    return false;
  }
  if (nullptr != value) {
    value->assign(result.cbegin(), result.cend());
  }
  set_last_error();
  return true;
}

bool DatabaseLevelDB::remove(const byte_array &key)
{
  if (!db_) {
    set_last_error(NotOpen);
    return false;
  }

  leveldb::Status status = db_->Delete(leveldb::WriteOptions(), slice(key));
  if (!status.ok()) {
    set_last_error_from_leveldb(status);
    return false;
  }
  set_last_error();
  return true;
}

bool DatabaseLevelDB::write_batch(const ItemList &items)
{
  if (!db_) {
    set_last_error(NotOpen);
    return false;
  }

  if (items.empty()) {
    set_last_error();
    return true;
  }

  leveldb::WriteBatch batch;
  for (const auto& it : items) {
    batch.Put(slice(it.first), slice(it.second));
  }

  leveldb::Status status = db_->Write(leveldb::WriteOptions(), &batch);
  if (!status.ok()) {
    set_last_error_from_leveldb(status);
    return false;
  }
  set_last_error();
  return true;
}

class DatabaseLevelDB::Iterator final : public Database::Iterator
{
public:
  Iterator(::leveldb::Iterator* it) :
    it_(it)
  {}
  ~Iterator()
  {}
  bool is_valid() const override final
  {
    return it_->Valid();
  }

  void seek_to_first() override final
  {
    it_->SeekToFirst();
  }

  void seek_to_last() override final
  {
    it_->SeekToLast();
  }

  void seek(const ::csdb::internal::byte_array &key) override final
  {
    it_->Seek(slice(key));
  }

  void next() override final
  {
    it_->Next();
  }

  void prev() override final
  {
    it_->Prev();
  }

  ::csdb::internal::byte_array key() const override final
  {
    if (it_->Valid()) {
      return to_byte_array(it_->key());
    } else {
      return ::csdb::internal::byte_array{};
    }
  }

  ::csdb::internal::byte_array value() const override final
  {
    if (it_->Valid()) {
      return to_byte_array(it_->value());
    } else {
      return ::csdb::internal::byte_array{};
    }
  }

private:
  std::unique_ptr<::leveldb::Iterator> it_;
};

DatabaseLevelDB::IteratorPtr DatabaseLevelDB::new_iterator()
{
  if (!db_) {
    set_last_error(NotOpen);
    return nullptr;
  }

  return Database::IteratorPtr(new DatabaseLevelDB::Iterator(db_->NewIterator(::leveldb::ReadOptions())));
}

} // namespace csdb
