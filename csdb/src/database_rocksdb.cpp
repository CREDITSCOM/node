#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

#include <boost/filesystem.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>
#include <rocksdb/table.h>
#include <rocksdb/write_batch.h>

#include <csdb/database_rocksdb.hpp>

namespace csdb {
namespace {

constexpr const char* kCfSeqNo = "seq_no";
constexpr const char* kCfContracts = "contracts";

// Big-endian so RocksDB's lex order matches numeric order.
inline std::string PackBE32(uint32_t v) {
    char b[4];
    b[0] = static_cast<char>((v >> 24) & 0xFF);
    b[1] = static_cast<char>((v >> 16) & 0xFF);
    b[2] = static_cast<char>((v >>  8) & 0xFF);
    b[3] = static_cast<char>( v        & 0xFF);
    return std::string(b, 4);
}

inline uint32_t UnpackBE32(const rocksdb::Slice& s) {
    if (s.size() != 4) return std::numeric_limits<uint32_t>::max();
    const auto* b = reinterpret_cast<const unsigned char*>(s.data());
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
           (uint32_t(b[2]) <<  8) | (uint32_t(b[3])      );
}

inline rocksdb::Slice BytesSlice(const cs::Bytes& v) {
    return rocksdb::Slice(reinterpret_cast<const char*>(v.data()), v.size());
}

inline cs::Bytes FromString(const std::string& s) {
    cs::Bytes out(s.size());
    if (!s.empty()) std::memcpy(out.data(), s.data(), s.size());
    return out;
}

}  // namespace

DatabaseRocksDB::DatabaseRocksDB() = default;

DatabaseRocksDB::~DatabaseRocksDB() {
    if (db_) {
        if (!bulk_load_) {
            db_->SyncWAL();
            rocksdb::FlushOptions fo;
            fo.wait = true;
            for (auto* cf : {cf_blocks_, cf_seq_no_, cf_contracts_})
                if (cf) db_->Flush(fo, cf);
        }
        if (cf_blocks_)    db_->DestroyColumnFamilyHandle(cf_blocks_);
        if (cf_seq_no_)    db_->DestroyColumnFamilyHandle(cf_seq_no_);
        if (cf_contracts_) db_->DestroyColumnFamilyHandle(cf_contracts_);
        cf_blocks_ = cf_seq_no_ = cf_contracts_ = nullptr;
    }
}

void DatabaseRocksDB::set_last_error_from_status(const rocksdb::Status& s) {
    if (s.ok()) { set_last_error(); return; }
    Error err = UnknownError;
    if      (s.IsNotFound())        err = NotFound;
    else if (s.IsCorruption())      err = Corruption;
    else if (s.IsNotSupported())    err = NotSupported;
    else if (s.IsInvalidArgument()) err = InvalidArgument;
    else if (s.IsIOError())         err = IOError;
    set_last_error(err, "RocksDB error: %s", s.ToString().c_str());
}

void DatabaseRocksDB::set_tuning(uint64_t block_cache_bytes, uint64_t memtable_bytes) {
    if (block_cache_bytes > 0) block_cache_bytes_ = block_cache_bytes;
    if (memtable_bytes > 0)    memtable_bytes_    = memtable_bytes;
}

bool DatabaseRocksDB::open(const std::string& path) {
    boost::filesystem::path direc(path);
    if (boost::filesystem::exists(direc)) {
        if (!boost::filesystem::is_directory(direc)) return false;
    } else {
        if (!boost::filesystem::create_directories(direc)) return false;
    }

    rocksdb::Options options;
    options.create_if_missing = true;
    options.create_missing_column_families = true;
    options.compression = rocksdb::kLZ4Compression;
    options.bottommost_compression = rocksdb::kLZ4HCCompression;
    options.level_compaction_dynamic_level_bytes = true;
    options.max_background_jobs = 8;
    options.write_buffer_size = memtable_bytes_;
    options.bytes_per_sync = 1u << 20;
    options.wal_bytes_per_sync = 1u << 20;
    options.wal_recovery_mode = rocksdb::WALRecoveryMode::kPointInTimeRecovery;
    options.paranoid_checks = true;
    options.compaction_pri = rocksdb::kMinOverlappingRatio;
    options.enable_pipelined_write = true;
    options.compaction_readahead_size = 2u << 20;
    options.max_open_files = -1;

    if (bulk_load_) {
        options.PrepareForBulkLoad();
    }

    auto block_cache = rocksdb::NewLRUCache(block_cache_bytes_);

    auto make_table_factory = [&](size_t block_size, bool partition) {
        rocksdb::BlockBasedTableOptions topt;
        topt.block_size = block_size;
        topt.block_cache = block_cache;
        topt.cache_index_and_filter_blocks = true;
        topt.pin_l0_filter_and_index_blocks_in_cache = true;
        topt.cache_index_and_filter_blocks_with_high_priority = true;
        topt.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
        topt.format_version = 5;
        topt.optimize_filters_for_memory = true;
        if (partition) {
            // Top-level only resident; sub-partitions cache on demand.
            topt.index_type = rocksdb::BlockBasedTableOptions::kTwoLevelIndexSearch;
            topt.partition_filters = true;
            topt.metadata_block_size = 4096;
            topt.pin_top_level_index_and_filter = true;
        }
        return std::shared_ptr<rocksdb::TableFactory>(rocksdb::NewBlockBasedTableFactory(topt));
    };

    rocksdb::ColumnFamilyOptions cf_blocks_opts(options);
    cf_blocks_opts.table_factory = make_table_factory(16 * 1024, false);
    cf_blocks_opts.target_file_size_base = 256ULL << 20;

    rocksdb::ColumnFamilyOptions cf_seq_no_opts(options);
    cf_seq_no_opts.table_factory = make_table_factory(4 * 1024, true);
    cf_seq_no_opts.write_buffer_size = 32ULL << 20;

    rocksdb::ColumnFamilyOptions cf_contracts_opts(options);
    cf_contracts_opts.table_factory = make_table_factory(16 * 1024, false);
    cf_contracts_opts.write_buffer_size = 32ULL << 20;

    std::vector<rocksdb::ColumnFamilyDescriptor> cfs = {
        {rocksdb::kDefaultColumnFamilyName, cf_blocks_opts},
        {kCfSeqNo,                          cf_seq_no_opts},
        {kCfContracts,                      cf_contracts_opts},
    };
    std::vector<rocksdb::ColumnFamilyHandle*> handles;

    rocksdb::DB* raw_db = nullptr;
    auto s = rocksdb::DB::Open(options, path, cfs, &handles, &raw_db);
    if (!s.ok()) {
        set_last_error_from_status(s);
        return false;
    }

    db_.reset(raw_db);
    cf_blocks_    = handles[0];
    cf_seq_no_    = handles[1];
    cf_contracts_ = handles[2];

    set_last_error();
    return true;
}

bool DatabaseRocksDB::is_open() const {
    return db_ != nullptr;
}

bool DatabaseRocksDB::put(const cs::Bytes& key, uint32_t seq_no, const cs::Bytes& value) {
    if (!db_) { set_last_error(NotOpen); return false; }

    const std::string be_key = PackBE32(seq_no + 1);
    // seq_no CF stores native-endian uint32 to match BDB byte pattern.
    const uint32_t native_seq = seq_no + 1;

    rocksdb::WriteBatch batch;
    batch.Put(cf_blocks_, rocksdb::Slice(be_key), BytesSlice(value));
    batch.Put(cf_seq_no_, BytesSlice(key),
              rocksdb::Slice(reinterpret_cast<const char*>(&native_seq), sizeof(native_seq)));

    rocksdb::WriteOptions wo;
    wo.sync = sync_writes_ && !bulk_load_;
    wo.disableWAL = bulk_load_;
    auto s = db_->Write(wo, &batch);
    if (!s.ok()) { set_last_error_from_status(s); return false; }
    set_last_error();
    return true;
}

bool DatabaseRocksDB::put_batch(const std::vector<PendingWrite>& items) {
    if (!db_) { set_last_error(NotOpen); return false; }
    if (items.empty()) { set_last_error(); return true; }

    // One WriteBatch over 2*N puts (blocks + seq_no), amortizing per-Write overhead.
    rocksdb::WriteBatch batch;
    std::vector<std::string> be_keys;
    std::vector<uint32_t> native_seqs;
    be_keys.reserve(items.size());
    native_seqs.reserve(items.size());

    for (const auto& item : items) {
        be_keys.push_back(PackBE32(item.seq_no + 1));
        native_seqs.push_back(item.seq_no + 1);
        batch.Put(cf_blocks_, rocksdb::Slice(be_keys.back()), BytesSlice(item.payload));
        batch.Put(cf_seq_no_, BytesSlice(item.hash_key),
                  rocksdb::Slice(reinterpret_cast<const char*>(&native_seqs.back()), sizeof(uint32_t)));
    }

    rocksdb::WriteOptions wo;
    wo.sync = sync_writes_ && !bulk_load_;
    wo.disableWAL = bulk_load_;
    auto s = db_->Write(wo, &batch);
    if (!s.ok()) { set_last_error_from_status(s); return false; }
    set_last_error();
    return true;
}

void DatabaseRocksDB::set_bulk_load(bool yes) {
    bulk_load_ = yes;
}

void DatabaseRocksDB::set_sync_writes(bool v) {
    sync_writes_ = v;
}

bool DatabaseRocksDB::compact_full() {
    if (!db_) { set_last_error(NotOpen); return false; }
    rocksdb::FlushOptions fo;
    fo.wait = true;
    for (auto* cf : {cf_blocks_, cf_seq_no_, cf_contracts_}) {
        if (!cf) continue;
        auto s = db_->Flush(fo, cf);
        if (!s.ok()) { set_last_error_from_status(s); return false; }
    }
    rocksdb::CompactRangeOptions co;
    co.exclusive_manual_compaction = true;
    for (auto* cf : {cf_blocks_, cf_seq_no_, cf_contracts_}) {
        if (!cf) continue;
        auto s = db_->CompactRange(co, cf, nullptr, nullptr);
        if (!s.ok()) { set_last_error_from_status(s); return false; }
    }
    set_last_error();
    return true;
}

bool DatabaseRocksDB::get(const cs::Bytes& key, cs::Bytes* value) {
    if (!db_) { set_last_error(NotOpen); return false; }

    if (value == nullptr) {
        // Existence probe on the seq_no CF (matches BDB's exists() check).
        std::string scratch;
        auto s = db_->Get(rocksdb::ReadOptions(), cf_seq_no_, BytesSlice(key), &scratch);
        return s.ok();
    }

    std::string seq_no_bytes;
    auto s = db_->Get(rocksdb::ReadOptions(), cf_seq_no_, BytesSlice(key), &seq_no_bytes);
    if (!s.ok()) { set_last_error_from_status(s); return false; }
    if (seq_no_bytes.size() != sizeof(uint32_t)) {
        set_last_error(Corruption, "seq_no entry has unexpected size");
        return false;
    }

    uint32_t native_seq = 0;
    std::memcpy(&native_seq, seq_no_bytes.data(), sizeof(native_seq));

    const std::string be_key = PackBE32(native_seq);
    std::string blob;
    s = db_->Get(rocksdb::ReadOptions(), cf_blocks_, rocksdb::Slice(be_key), &blob);
    if (!s.ok()) { set_last_error_from_status(s); return false; }

    *value = FromString(blob);
    set_last_error();
    return true;
}

bool DatabaseRocksDB::seq_no(const cs::Bytes& key, uint32_t* value) {
    if (value == nullptr) { set_last_error(InvalidArgument); return false; }
    if (!db_)             { set_last_error(NotOpen);         return false; }

    std::string buf;
    auto s = db_->Get(rocksdb::ReadOptions(), cf_seq_no_, BytesSlice(key), &buf);
    if (!s.ok()) { set_last_error_from_status(s); return false; }
    if (buf.size() != sizeof(uint32_t)) {
        set_last_error(Corruption, "seq_no entry has unexpected size");
        return false;
    }
    std::memcpy(value, buf.data(), sizeof(uint32_t));
    return true;
}

bool DatabaseRocksDB::get(const uint32_t seq_no, cs::Bytes* value) {
    if (!db_) { set_last_error(NotOpen); return false; }
    if (value == nullptr) return false;

    const std::string be_key = PackBE32(seq_no + 1);
    std::string blob;
    auto s = db_->Get(rocksdb::ReadOptions(), cf_blocks_, rocksdb::Slice(be_key), &blob);
    if (!s.ok()) { set_last_error_from_status(s); return false; }

    *value = FromString(blob);
    set_last_error();
    return true;
}

bool DatabaseRocksDB::remove(const cs::Bytes& key) {
    if (!db_) { set_last_error(NotOpen); return false; }

    std::string seq_no_bytes;
    auto s = db_->Get(rocksdb::ReadOptions(), cf_seq_no_, BytesSlice(key), &seq_no_bytes);
    if (!s.ok()) { set_last_error_from_status(s); return false; }
    if (seq_no_bytes.size() != sizeof(uint32_t)) {
        set_last_error(Corruption, "seq_no entry has unexpected size");
        return false;
    }
    uint32_t native_seq = 0;
    std::memcpy(&native_seq, seq_no_bytes.data(), sizeof(native_seq));
    const std::string be_key = PackBE32(native_seq);

    rocksdb::WriteBatch batch;
    batch.Delete(cf_seq_no_, BytesSlice(key));
    batch.Delete(cf_blocks_, rocksdb::Slice(be_key));
    rocksdb::WriteOptions wo;
    wo.sync = sync_writes_ && !bulk_load_;
    wo.disableWAL = bulk_load_;
    s = db_->Write(wo, &batch);
    if (!s.ok()) { set_last_error_from_status(s); return false; }

    set_last_error();
    return true;
}

bool DatabaseRocksDB::write_batch(const ItemList&) {
    assert(false);   // unimplemented; mirrors BDB
    if (!db_) { set_last_error(NotOpen); return false; }
    set_last_error();
    return true;
}

class DatabaseRocksDB::Iterator final : public Database::Iterator {
public:
    explicit Iterator(rocksdb::Iterator* it) : it_(it), valid_(it != nullptr) {}
    ~Iterator() final { delete it_; }

    bool is_valid() const final { return valid_ && it_ && it_->Valid(); }

    void seek_to_first() final {
        if (!it_) return;
        it_->SeekToFirst();
        valid_ = it_->Valid();
    }

    void seek_to_last() final {
        if (!it_) return;
        it_->SeekToLast();
        valid_ = it_->Valid();
    }

    void seek(const cs::Bytes&) final {
        // BDB equivalent is also asserted-out; preserve the contract.
        assert(false);
    }

    void seek(const uint32_t seq_no) final {
        if (!it_) return;
        const std::string be_key = PackBE32(seq_no + 1);
        it_->Seek(rocksdb::Slice(be_key));
        // BDB DB_SET semantics: only valid on exact match.
        if (it_->Valid() && it_->key().size() == 4 && it_->key().compare(rocksdb::Slice(be_key)) == 0) {
            valid_ = true;
        } else {
            valid_ = false;
        }
    }

    void next() final {
        if (!it_) return;
        it_->Next();
        valid_ = it_->Valid();
    }

    void prev() final {
        // BDB equivalent is also asserted-out; preserve the contract.
        assert(false);
    }

    uint32_t key() const final {
        if (!valid_ || !it_) return std::numeric_limits<uint32_t>::max();
        const auto v = UnpackBE32(it_->key());
        // Storage convention is 1-based (seq_no+1); callers expect 0-based.
        return (v == std::numeric_limits<uint32_t>::max()) ? v : v - 1;
    }

    cs::Bytes value() const final {
        if (!valid_ || !it_) return cs::Bytes{};
        const auto v = it_->value();
        cs::Bytes out(v.size());
        if (!v.empty()) std::memcpy(out.data(), v.data(), v.size());
        return out;
    }

private:
    rocksdb::Iterator* it_;
    bool valid_;
};

DatabaseRocksDB::IteratorPtr DatabaseRocksDB::new_iterator() {
    if (!db_) {
        set_last_error(NotOpen);
        return nullptr;
    }
    return Database::IteratorPtr(new DatabaseRocksDB::Iterator(
        db_->NewIterator(rocksdb::ReadOptions(), cf_blocks_)));
}

bool DatabaseRocksDB::flush() {
    if (!db_) { set_last_error(NotOpen); return false; }
    auto s = db_->SyncWAL();
    if (!s.ok()) { set_last_error_from_status(s); return false; }
    set_last_error();
    return true;
}

bool DatabaseRocksDB::updateContractData(const cs::Bytes& key, const cs::Bytes& data) {
    if (!db_) { set_last_error(NotOpen); return false; }
    rocksdb::WriteOptions wo;
    wo.sync = sync_writes_ && !bulk_load_;
    wo.disableWAL = bulk_load_;
    auto s = db_->Put(wo, cf_contracts_, BytesSlice(key), BytesSlice(data));
    if (!s.ok()) { set_last_error_from_status(s); return false; }
    set_last_error();
    return true;
}

bool DatabaseRocksDB::getContractData(const cs::Bytes& key, cs::Bytes& data) {
    if (!db_) { set_last_error(NotOpen); return false; }
    std::string buf;
    auto s = db_->Get(rocksdb::ReadOptions(), cf_contracts_, BytesSlice(key), &buf);
    if (!s.ok()) { set_last_error_from_status(s); return false; }
    data = FromString(buf);
    set_last_error();
    return true;
}

}  // namespace csdb
