// RocksDB-backed csdb::Database. CFs: blocks, seq_no, contracts.
// blocks keyed BE(seq+1). Selected via -DCSDB_BACKEND=rocksdb.

#ifndef _CREDITS_CSDB_DATABASE_ROCKSDB_H_INCLUDED_
#define _CREDITS_CSDB_DATABASE_ROCKSDB_H_INCLUDED_

#include <memory>
#include <string>

#include <csdb/database.hpp>

namespace rocksdb {
class DB;
class ColumnFamilyHandle;
class Status;
}  // namespace rocksdb

namespace csdb {

class DatabaseRocksDB : public Database {
public:
    DatabaseRocksDB();
    ~DatabaseRocksDB() override;

public:
    // Bulk-load mode (before open): no auto-compaction, disableWAL; caller must compact_full() before close.
    void set_bulk_load(bool yes);
    // Per-write fsync toggle (default off; durability anchored at checkpoint via flush()).
    void set_sync_writes(bool v);

    // Flush + full-range compaction across all CFs; call once after a bulk-load run.
    bool compact_full();

    // Override RocksDB cache/memtable budget (before open); 0 keeps defaults (1 GiB / 256 MiB).
    void set_tuning(uint64_t block_cache_bytes, uint64_t memtable_bytes);

    bool open(const std::string& path);

private:
    bool is_open() const final;
    bool put(const cs::Bytes& key, uint32_t seq_no, const cs::Bytes& value) final;
    bool put_batch(const std::vector<PendingWrite>& items) final;
    bool get(const cs::Bytes& key, cs::Bytes* value) final;
    bool get(const uint32_t seq_no, cs::Bytes* value) final;
    bool remove(const cs::Bytes&) final;
    bool seq_no(const cs::Bytes& key, uint32_t* value) final;
    bool write_batch(const ItemList&) final;
    IteratorPtr new_iterator() final;

    bool updateContractData(const cs::Bytes& key, const cs::Bytes& data) override;
    bool getContractData(const cs::Bytes& key, cs::Bytes& data) override;
    bool flush() override;

private:
    class Iterator;
    void set_last_error_from_status(const rocksdb::Status& s);

private:
    std::unique_ptr<rocksdb::DB> db_;
    rocksdb::ColumnFamilyHandle* cf_blocks_ = nullptr;     // default CF
    rocksdb::ColumnFamilyHandle* cf_seq_no_ = nullptr;
    rocksdb::ColumnFamilyHandle* cf_contracts_ = nullptr;
    bool bulk_load_ = false;
    bool sync_writes_ = false;   // matches BDB's DB_TXN_NOSYNC; durability anchored at checkpoint via flush()
    uint64_t block_cache_bytes_ = 1ULL << 30;      // 1 GiB
    uint64_t memtable_bytes_    = 256ULL << 20;    // 256 MiB
};

}  // namespace csdb

#endif  // _CREDITS_CSDB_DATABASE_ROCKSDB_H_INCLUDED_
