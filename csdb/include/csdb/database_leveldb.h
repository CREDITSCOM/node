/**
  * @file database_leveldb.h
  * @author Evgeny Zalivochkin
  */

#pragma once
#ifndef _CREDITS_CSDB_DATABASE_LEVELDB_H_INCLUDED_
#define _CREDITS_CSDB_DATABASE_LEVELDB_H_INCLUDED_

#include <memory>
#include "csdb/database.h"

namespace leveldb {
class DB;
class Status;
struct Options;
} // namespace leveldb

namespace csdb {

class DatabaseLevelDB : public Database
{
public:
  DatabaseLevelDB();
  ~DatabaseLevelDB();

public:
  bool open(const std::string& path);
  bool open(const std::string& path, const leveldb::Options& options);

private:
  bool is_open() const override final;
  bool put(const byte_array &key, const byte_array &value) override final;
  bool get(const byte_array &key, byte_array *value) override final;
  bool remove(const byte_array &key) override final;
  bool write_batch(const ItemList &items) override final;
  IteratorPtr new_iterator() override final;

private:
  class Iterator;

private:
  void set_last_error_from_leveldb(const ::leveldb::Status& status);

private:
  std::unique_ptr<leveldb::DB> db_;
};

} // namespace csdb
#endif // _CREDITS_CSDB_DATABASE_LEVELDB_H_INCLUDED_
