#include <db_cxx.h>
#include <cassert>
#include <cstdlib>
#include <exception>

#include <boost/filesystem.hpp>

#include "csdb/database_berkeleydb.hpp"
#include "csdb/pool.hpp"

#include "priv_crypto.hpp"

#include <lib/system/scopeguard.hpp>

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
struct Dbt_copy<cs::Bytes> : public Dbt {
    explicit Dbt_copy(const cs::Bytes &data) {
        set_data(const_cast<unsigned char *>(data.data()));
        set_size(static_cast<uint32_t>(data.size()));
        set_ulen(static_cast<uint32_t>(data.size()));
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
, db_seq_no_(nullptr)
, db_smart_states_(nullptr) {
}

DatabaseBerkeleyDB::~DatabaseBerkeleyDB() {
    std::cout << "Attempt db_blocks_ to close...\n" << std::flush;
    db_blocks_->close(0);
    std::cout << "DB db_blocks_ was closed.\n" << std::flush;
    std::cout << "Attempt db_seq_no_ to close...\n" << std::flush;
    db_seq_no_->close(0);
    std::cout << "DB db_seq_no_ was closed.\n" << std::flush;
    std::cout << "Attempt db_smart_states_ to close...\n" << std::flush;
    db_smart_states_->close(0);
    std::cout << "DB db_smart_states_ was closed.\n" << std::flush;
#ifdef TRANSACTIONS_INDEX
    db_trans_idx_->close(0);
#endif
    std::cout << "DB db_smart_states_ was closed.\n" << std::flush;
    env_.close(0);
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

    env_.log_set_config(DB_LOG_AUTO_REMOVE, 1);

    uint32_t db_env_open_flags = DB_CREATE | DB_INIT_MPOOL | DB_THREAD | DB_RECOVER | DB_INIT_TXN | DB_INIT_LOCK;
    int status = env_.open(path.c_str(), db_env_open_flags, 0);
    status = status ? status : env_.set_flags(DB_TXN_NOSYNC, 1);

    env_.txn_checkpoint(1024 * 1024, 0, 0);

    DbTxn *txn;
    status = status ? status : env_.txn_begin(nullptr, &txn, DB_READ_UNCOMMITTED);
    auto txn_create_status = status;

    auto g = cs::scopeGuard([&]() {
        if (txn_create_status) {
            return;
        }
        if (status) {
            txn->abort();
        }
        else {
            txn->commit(0);
        }
    });

#ifdef TRANSACTIONS_INDEX
    auto db_trans_idx = new Db(&env_, 0);
#endif

    if (!status) {
        decltype(db_blocks_) db_blocks(new Db(&env_, 0));
        status = db_blocks->open(txn, "blockchain.db", NULL, DB_RECNO, DB_CREATE | DB_READ_UNCOMMITTED, 0);
        db_blocks_.swap(db_blocks);
    }
    if (!status) {
        decltype(db_seq_no_) db_seq_no(new Db(&env_, 0));
        status = db_seq_no->open(txn, "sequence.db", NULL, DB_HASH, DB_CREATE | DB_READ_UNCOMMITTED, 0);
        db_seq_no_.swap(db_seq_no);
    }
    if (status == 0) {
        decltype(db_smart_states_) db_smart_states(new Db(&env_, 0));
        status = db_smart_states->open(NULL, "contracts.db", NULL, DB_HASH, DB_CREATE | DB_READ_UNCOMMITTED, 0);
        db_smart_states_.swap(db_smart_states);
    }
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }

#ifdef TRANSACTIONS_INDEX
    status = db_trans_idx->open(NULL, "index.db", NULL, DB_BTREE, DB_CREATE, 0);
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }
    db_trans_idx_.reset(db_trans_idx);
#endif

    set_last_error();
    return true;
}

bool DatabaseBerkeleyDB::is_open() const {
    return static_cast<bool>(db_blocks_);
}

bool DatabaseBerkeleyDB::put(const cs::Bytes &key, uint32_t seq_no, const cs::Bytes &value) {
    if (!db_blocks_) {
        set_last_error(NotOpen);
        return false;
    }

    DbTxn *tid;
    int status = env_.txn_begin(nullptr, &tid, DB_READ_UNCOMMITTED);
    int txn_create_status = status;
    auto g = cs::scopeGuard([&]() {
        if (txn_create_status) {
            return;
        }
        if (status) {
            tid->abort();
        }
        else {
            tid->commit(0);
        }
    });
    Dbt_copy<uint32_t> db_seq_no(seq_no + 1);
    if (!status) {
        Dbt_copy<cs::Bytes> db_value(value);
        status = db_blocks_->put(tid, &db_seq_no, &db_value, 0);
    }
    if (!status) {
        Dbt_copy<cs::Bytes> db_key(key);
        status = db_seq_no_->put(tid, &db_key, &db_seq_no, 0);
    }

    if (!status) {
        set_last_error();
        return true;
    }
    else {
        set_last_error_from_berkeleydb(status);
        return false;
    }
}

bool DatabaseBerkeleyDB::get(const cs::Bytes &key, cs::Bytes *value) {
    if (!db_blocks_) {
        set_last_error(NotOpen);
        return false;
    }

    Dbt_copy<cs::Bytes> db_key(key);
    if (value == nullptr) {
        return db_seq_no_->exists(nullptr, &db_key, 0) == 0;
    }

    Dbt_copy<uint32_t> db_seq_no;
    int status = db_seq_no_->get(nullptr, &db_key, &db_seq_no, DB_READ_UNCOMMITTED);
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }

    Dbt_safe db_value;

    status = db_blocks_->get(nullptr, &db_seq_no, &db_value, DB_READ_UNCOMMITTED);
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }

    auto begin = static_cast<uint8_t *>(db_value.get_data());
    value->assign(begin, begin + db_value.get_size());
    set_last_error();
    return true;
}

bool DatabaseBerkeleyDB::get(const uint32_t seq_no, cs::Bytes *value) {
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

bool DatabaseBerkeleyDB::remove(const cs::Bytes &key) {
    if (!db_blocks_) {
        set_last_error(NotOpen);
        return false;
    }

    Dbt_copy<cs::Bytes> db_key(key);
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

bool DatabaseBerkeleyDB::write_batch(const ItemList &) {
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

    void seek(const cs::Bytes &) final {
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

    cs::Bytes key() const final {
        return cs::Bytes{};
    }

    cs::Bytes value() const final {
        if (valid_) {
            return value_;
        }
        return cs::Bytes{};
    }

private:
    void set_value(const Dbt &value) {
        auto begin = static_cast<uint8_t *>(value.get_data());
        value_.assign(begin, begin + value.get_size());
    }

    Dbc *it_;
    bool valid_;
    cs::Bytes value_;
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

#ifdef TRANSACTIONS_INDEX
bool DatabaseBerkeleyDB::putToTransIndex(const cs::Bytes &key, const cs::Bytes &value) {
    if (!db_trans_idx_) {
        set_last_error(NotOpen);
        return false;
    }

    Dbt_copy<cs::Bytes> db_key(key);
    Dbt_copy<cs::Bytes> db_value(value);

    int status = db_trans_idx_->put(nullptr, &db_key, &db_value, 0);
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }

    set_last_error();
    return true;
}

bool DatabaseBerkeleyDB::getFromTransIndex(const cs::Bytes &key, cs::Bytes *value) {
    if (!db_trans_idx_) {
        set_last_error(NotOpen);
        return false;
    }

    Dbt_copy<cs::Bytes> db_key(key);
    Dbt_safe db_value;

    int status = db_trans_idx_->get(nullptr, &db_key, &db_value, 0);
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }

    auto begin = reinterpret_cast<uint8_t *>(db_value.get_data());
    value->assign(begin, begin + db_value.get_size());
    set_last_error();
    return true;
}
#endif

bool DatabaseBerkeleyDB::updateContractData(const cs::Bytes& key, const cs::Bytes& data) {
    if (!db_smart_states_) {
        set_last_error(NotOpen);
        return false;
    }

    Dbt_copy<cs::Bytes> db_key(key);
    Dbt_copy<cs::Bytes> db_value(data);

    int status = db_smart_states_->put(nullptr, &db_key, &db_value, 0);
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }

    set_last_error();
    return true;
}

bool DatabaseBerkeleyDB::getContractData(const cs::Bytes& key, cs::Bytes& data) {
    if (!db_smart_states_) {
        set_last_error(NotOpen);
        return false;
    }

    Dbt_copy<cs::Bytes> db_key(key);
    Dbt_safe db_value;

    int status = db_smart_states_->get(nullptr, &db_key, &db_value, 0);
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }

    auto begin = reinterpret_cast<uint8_t*>(db_value.get_data());
    data.assign(begin, begin + db_value.get_size());
    set_last_error();
    return true;
}

}  // namespace csdb
