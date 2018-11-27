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
  ~DatabaseBerkeleyDB() override;

public:
  bool open(const std::string &path);

private:
  bool is_open() const final;
  bool put(const byte_array &key, uint32_t seq_no, const byte_array &value) final;
  bool get(const byte_array &key, byte_array *value) final;
  bool get(const uint32_t seq_no, byte_array *value) final;
  bool remove(const byte_array &) final;
  bool write_batch(const ItemList &) final;
  IteratorPtr new_iterator() final;

#ifdef TRANSACTIONS_INDEX
  bool putToTransIndex(const byte_array &key, const byte_array &value) override final;
  bool getFromTransIndex(const byte_array &key, byte_array *value) override final;
#endif

private:
  class Iterator;

private:
  void set_last_error_from_berkeleydb(int status);

private:
  DbEnv env_;
  std::unique_ptr<Db> db_blocks_;
  std::unique_ptr<Db> db_seq_no_;
#ifdef TRANSACTIONS_INDEX
  std::unique_ptr<Db> db_trans_idx_;
#endif
};

}  // namespace csdb
#endif  // _CREDITS_CSDB_DATABASE_BERKELEYDB_H_INCLUDED_
