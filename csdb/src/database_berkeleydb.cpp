#include <db_cxx.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include <boost/filesystem.hpp>

#include <csdb/database_berkeleydb.hpp>
#include <csdb/pool.hpp>

#include "priv_crypto.hpp"

#include <lib/system/scopeguard.hpp>

namespace csdb {

bool needsRecovery(const std::exception& e) {
    if (dynamic_cast<const DbRunRecoveryException*>(&e) != nullptr) {
        return true;
    }
    const auto* db = dynamic_cast<const DbException*>(&e);
    return db != nullptr && db->get_errno() == DB_RUNRECOVERY;
}


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
: env_(0u)
, db_blocks_(nullptr)
, db_seq_no_(nullptr)
, db_contracts_(nullptr) {}

DatabaseBerkeleyDB::~DatabaseBerkeleyDB() {
    // A destructor must not throw: a handle of a panicked environment (e.g. after ENOSPC) throws from
    // close(), which during the unwinding of another exception would terminate the process. After a
    // failed open some handles were never created.
    auto closeDb = [](Db* db, const char* name) {
        if (!db) return;
        try {
            std::cout << "Attempt " << name << " to close...\n" << std::flush;
            db->close(0);
            std::cout << "DB " << name << " was closed.\n" << std::flush;
        }
        catch (const std::exception& e) {
            std::cout << "DB " << name << " could not be closed: " << e.what() << "\n" << std::flush;
        }
    };
    closeDb(db_blocks_.get(), "db_blocks_");
    closeDb(db_seq_no_.get(), "db_seq_no_");
    closeDb(db_contracts_.get(), "db_contracts_");
    if (logfile_thread_.joinable()) {
        quit_ = true;
        logfile_thread_.join();
    }
    try {
        env_.close(0);
    }
    catch (const std::exception& e) {
        std::cout << "BerkeleyDB environment could not be closed: " << e.what() << "\n" << std::flush;
    }
}

void DatabaseBerkeleyDB::logfile_routine() {
    int cnt = 0;
    /* Check once every 5 minutes. */
    for (;; std::this_thread::sleep_for(std::chrono::seconds(1))) {
        if (quit_) break;
        if (++cnt % 300 == 0) {
            int ret;
            char **begin, **list;
            env_.txn_checkpoint(0, 0, DB_FORCE);

            /* Get the list of log files. */
            if (env_.log_archive(&list, DB_ARCH_ABS) != 0) {
                continue;
            }

            /* Remove the log files. */
            if (list != 0) {
                for (begin = list; *list != NULL; ++list) {
                    if ((ret = ::remove(*list)) != 0) {
                        cslog() << "Can't remove " << *list << " error = " << ret;
                    }
                }
                free(begin);
            }
        }
    }
}

// Commits may leave their log records in the in-process log buffer (the environment does not sync
// every commit). Write and sync the log, or a checkpoint saved after this call could name blocks
// that a killed process never put on disk, and the next start would refuse the database.
bool DatabaseBerkeleyDB::flush() {
    if (panicked_) {
        set_last_error(Corruption, "the BerkeleyDB environment needs recovery");
        return false;
    }
    try {
        const int status = env_.log_flush(nullptr);
        set_last_error_from_berkeleydb(status);
        return status == 0;
    }
    catch (const DbException& e) {
        set_last_error_from_berkeleydb(e.get_errno());
        return false;
    }
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
    db_contracts_.reset(nullptr);

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

    if (!status) {
        auto db_blocks = new Db(&env_, 0);
        status = db_blocks->open(txn, "blockchain.db", NULL, DB_RECNO, DB_CREATE | DB_READ_UNCOMMITTED, 0);
        db_blocks_.reset(db_blocks);
    }
    if (!status) {
        auto db_seq_no = new Db(&env_, 0);
        status = db_seq_no->open(txn, "sequence.db", NULL, DB_HASH, DB_CREATE | DB_READ_UNCOMMITTED, 0);
        db_seq_no_.reset(db_seq_no);
    }
    if (status == 0) {
        // until the explanation found suppress exceptions particularly on db close()
        auto db_contracts = new Db(&env_, 0/*DB_CXX_NO_EXCEPTIONS*/);
        status = db_contracts->open(txn, "contracts.db", NULL, DB_HASH, DB_CREATE | DB_READ_UNCOMMITTED, 0);
        db_contracts_.reset(db_contracts);
    }
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }

    logfile_thread_ = std::thread(&DatabaseBerkeleyDB::logfile_routine, this);

    set_last_error();
    return true;
}

bool DatabaseBerkeleyDB::is_open() const {
    return static_cast<bool>(db_blocks_);
}

// Runs `body` in a transaction committed only when it returns 0. BerkeleyDB reports errors by
// exception (the handles are opened without DB_CXX_NO_EXCEPTIONS): a full disk, a panicked
// environment. They become a failed call with the error set, never an exception to the caller, and
// the commit is checked: a commit that fails is a failed write, not a silent success.
template <typename Body>
bool DatabaseBerkeleyDB::inTransaction(const char* what, Body body) {
    DbTxn* tid = nullptr;
    try {
        int status = env_.txn_begin(nullptr, &tid, DB_READ_UNCOMMITTED);
        if (status) {
            tid = nullptr;
            set_last_error_from_berkeleydb(status);
            return false;
        }
        status = body(tid);
        if (status) {
            DbTxn* t = tid;
            tid = nullptr;
            t->abort();
            set_last_error_from_berkeleydb(status);
            return false;
        }
        DbTxn* t = tid;
        tid = nullptr;    // a transaction handle is gone once commit returns, even when it failed
        status = t->commit(0);
        if (status) {
            set_last_error_from_berkeleydb(status);
            return false;
        }
    }
    catch (const std::exception& e) {
        if (tid) {
            try { tid->abort(); } catch (...) {}
        }
        if (needsRecovery(e)) {
            panic(what, e);    // throws
        }
        set_last_error(UnknownError, "%s: %s", what, e.what());
        return false;
    }
    set_last_error();
    return true;
}

bool DatabaseBerkeleyDB::put(const cs::Bytes &key, uint32_t seq_no, const cs::Bytes &value) {
    if (!db_blocks_) {
        set_last_error(NotOpen);
        return false;
    }
    return inTransaction("put", [&](DbTxn* tid) {
        Dbt_copy<uint32_t> db_seq_no(seq_no + 1);
        Dbt_copy<cs::Bytes> db_value(value);
        int status = db_blocks_->put(tid, &db_seq_no, &db_value, 0);
        if (!status) {
            Dbt_copy<cs::Bytes> db_key(key);
            status = db_seq_no_->put(tid, &db_key, &db_seq_no, 0);
        }
        return status;
    });
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

// sequnce from block hash
bool DatabaseBerkeleyDB::seq_no(const cs::Bytes& key, uint32_t* value) {
    if (value == nullptr) {
        set_last_error(InvalidArgument);
        return false;
    }
    if (!db_blocks_) {
        set_last_error(NotOpen);
        return false;
    }

    Dbt_copy<cs::Bytes> db_key(key);
    Dbt_copy<uint32_t> db_seq_no;
    int status = db_seq_no_->get(nullptr, &db_key, &db_seq_no, DB_READ_UNCOMMITTED);
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }
    
    *value = *static_cast<uint32_t*>(db_seq_no.get_data());
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
    // storage wants to load blocks by 0-based index: 1 => pool[0], 2 => pool[1] etc.
    Dbt_copy<uint32_t> db_seq_no(seq_no + 1);

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
            set_key(key);
            set_value(value);
            valid_ = true;
        }
        else {
            valid_ = false;
        }
    }

    void seek_to_last() final {
        if (it_ == nullptr) {
            return;
        }

        Dbt key;
        Dbt_safe value;

        int ret = it_->get(&key, &value, DB_LAST);
        if (ret == 0) {
            set_key(key);
            set_value(value);
            valid_ = true;
        }
        else {
            valid_ = false;
        }
    }

    void seek(const cs::Bytes &) final {
        assert(false);
    }

    void seek(const uint32_t seq_no) final {
        if (it_ == nullptr) {
            return;
        }

        // storage wants to load blocks by 0-based index:
        // 1 => pool[0], 2 => pool[1] etc.
        Dbt_copy<uint32_t> key(seq_no + 1);
        Dbt_safe value;

        int ret = it_->get(&key, &value, DB_SET);
        if (ret == 0) {
            set_key(key);
            set_value(value);
            valid_ = true;
        }
        else {
            valid_ = false;
        }
    }

    void next() override final {
        if (it_ == nullptr) {
            return;
        }

        Dbt key;
        Dbt_safe value;

        int ret = it_->get(&key, &value, DB_NEXT);
        if (ret == 0) {
            set_key(key);
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

    uint32_t key() const final {
        if (valid_) {
            return key_;
        }
        return std::numeric_limits<uint32_t>::max();
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

    void set_key(const Dbt& key) {
        auto begin = static_cast<uint8_t*>(key.get_data());
        cs::Bytes tmp;
        tmp.assign(begin, begin + key.get_size());
        if (tmp.size() == sizeof(uint32_t)) {
            // storage wants to use 0-based keys: 1 => pool[0], 2 => pool[1] etc.
            key_ = *reinterpret_cast<uint32_t*>(tmp.data()) - 1;
        }
        else {
            key_ = std::numeric_limits<uint32_t>::max();
        }
    }

    Dbc *it_;
    bool valid_;
    cs::Bytes value_;
    uint32_t key_;
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

bool DatabaseBerkeleyDB::updateContractData(const cs::Bytes& key, const cs::Bytes& data) {
    if (!db_contracts_) {
        set_last_error(NotOpen);
        return false;
    }
    return inTransaction("updateContractData", [&](DbTxn* tid) {
        Dbt_copy<cs::Bytes> db_key(key);
        Dbt_copy<cs::Bytes> db_value(data);
        return db_contracts_->put(tid, &db_key, &db_value, 0);
    });
}

bool DatabaseBerkeleyDB::getContractData(const cs::Bytes& key, cs::Bytes& data) {
    if (!db_contracts_) {
        set_last_error(NotOpen);
        return false;
    }

    Dbt_copy<cs::Bytes> db_key(key);
    Dbt_safe db_value;

    int status = db_contracts_->get(nullptr, &db_key, &db_value, 0);
    if (status) {
        set_last_error_from_berkeleydb(status);
        return false;
    }

    auto begin = reinterpret_cast<uint8_t*>(db_value.get_data());
    data.assign(begin, begin + db_value.get_size());
    set_last_error();
    return true;
}

void DatabaseBerkeleyDB::panic(const char* what, const std::exception& e) {
    // the environment is unusable until a process opens it again with DB_RECOVER: report it and let
    // the exception out, so that the node stops (and its supervisor restarts it) instead of failing
    // every write for ever
    panicked_ = true;
    set_last_error(Corruption, "%s: the BerkeleyDB environment needs recovery (restart the node): %s", what, e.what());
    throw DbRunRecoveryException(e.what());
}

}  // namespace csdb
