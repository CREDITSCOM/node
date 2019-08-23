/**
 * @file database_berkeleydb.h
 * @author Evgeny Zalivochkin
 */

#ifndef _CREDITS_CSDB_DATABASE_BERKELEY_H_INCLUDED_
#define _CREDITS_CSDB_DATABASE_BERKELEY_H_INCLUDED_

#include <db_cxx.h>
#include <memory>
#include <thread>

#include <csdb/database.hpp>

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
    bool open(const std::string& path);

private:
    bool is_open() const final;
    bool put(const cs::Bytes& key, uint32_t seq_no, const cs::Bytes& value) final;
    bool get(const cs::Bytes& key, cs::Bytes* value) final;
    bool get(const uint32_t seq_no, cs::Bytes* value) final;
    bool remove(const cs::Bytes&) final;
    bool seq_no(const cs::Bytes& key, uint32_t* value) final; // sequnce from block hash
    bool write_batch(const ItemList&) final;
    IteratorPtr new_iterator() final;

    bool putToTransIndex(const cs::Bytes& key, const cs::Bytes& value) override final;
    bool getFromTransIndex(const cs::Bytes& key, cs::Bytes* value) override final;
    void truncateTransIndex() override final;

    bool updateContractData(const cs::Bytes& key, const cs::Bytes& data) override;
    bool getContractData(const cs::Bytes& key, cs::Bytes& data) override;

    void logfile_routine();

private:
    class Iterator;

private:
    void set_last_error_from_berkeleydb(int status);

private:
    DbEnv env_;
    std::unique_ptr<Db> db_blocks_;
    std::unique_ptr<Db> db_seq_no_;
    std::unique_ptr<Db> db_contracts_;
    std::unique_ptr<Db> db_trans_idx_;
    std::thread logfile_thread_;
    bool quit_ = false;
};

}  // namespace csdb
#endif  // _CREDITS_CSDB_DATABASE_BERKELEYDB_H_INCLUDED_
