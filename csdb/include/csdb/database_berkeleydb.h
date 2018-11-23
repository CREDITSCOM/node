/**
 * @file database_berkeleydb.h
 * @author Evgeny Zalivochkin
 */

#ifndef _CREDITS_CSDB_DATABASE_BERKELEY_H_INCLUDED_
#define _CREDITS_CSDB_DATABASE_BERKELEY_H_INCLUDED_

#include <db_cxx.h>
#include <memory>

#include "csdb/database.h"

namespace berkeleydb {
class DB;
class Status;
struct Options;
}  // namespace berkeleydb

namespace csdb {

class DatabaseBerkeleyDB : public Database {
public:
  DatabaseBerkeleyDB();
  ~DatabaseBerkeleyDB();

public:
  bool open(const std::string &path);

private:
  bool is_open() const override final;
  bool put(const byte_array &key, uint32_t seq_no, const byte_array &value) override final;
  bool get(const byte_array &key, byte_array *value) override final;
  bool get(const uint32_t seq_no, byte_array *value) override final;
  bool remove(const byte_array &) override final;
  bool write_batch(const ItemList &) override final;
  IteratorPtr new_iterator() override final;

private:
  class Iterator;

private:
  void set_last_error_from_berkeleydb(int status);

private:
  DbEnv env_;
  std::unique_ptr<Db> db_blocks_;
  std::unique_ptr<Db> db_seq_no_;
};

}  // namespace csdb
#endif  // _CREDITS_CSDB_DATABASE_BERKELEYDB_H_INCLUDED_
