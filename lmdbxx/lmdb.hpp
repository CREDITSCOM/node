#ifndef LMDBXX_HPP
#define LMDBXX_HPP

#include <cassert>
#include <numeric>
#include <charconv>
#include <string_view>

#include <lmdbexception.hpp>

#include <lib/system/signals.hpp>
#include <lib/system/reflection.hpp>

namespace cs {
using FlushSignal = cs::Signal<void()>;
using CommitSignal = cs::Signal<void(const char* data, size_t size)>;
using RemoveSignal = cs::Signal<void(const char* data, size_t size)>;
using FailureSignal = cs::Signal<void(const LmdbException& error)>;
using IncreaseSignal = cs::Signal<void(size_t size)>;

// lmdbxx RAII wrapper, not thread safe by default
class Lmdb {
    using Info = MDB_envinfo;
    using Stats = MDB_stat;
public:
    enum Options : size_t {
        DefaultMapSize = 10485760,
        Default1GbMapSize = 1UL * 1024UL * 1024UL * 1024UL,
        DefaultIncreaseSize = DefaultMapSize/2
    };

    enum Flags : unsigned int {
        DefaultEnvFlags = MDB_NOSYNC | MDB_WRITEMAP | MDB_MAPASYNC
    };

    explicit Lmdb(const std::string& path, const unsigned int flags = lmdb::env::default_flags);
    ~Lmdb() noexcept;

    /// database settings

    // opens lmdb
    void open(const unsigned int flags = Flags::DefaultEnvFlags,
              const lmdb::mode mode = lmdb::env::default_mode) {
        try {
            env_.open(path_.c_str(), flags, mode);
            isOpen_ = true;
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    void close() {
        env_.close();
        isOpen_ = false;
    }

    // returns database open status
    bool isOpen() const {
        return isOpen_;
    }

    void setFlags(const unsigned int flags, const bool onoff = true) {
        try {
            env_.set_flags(flags, onoff);
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    void setMaxReaders(std::size_t count) {
        try {
            env_.set_max_readers(static_cast<unsigned int>(count));
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    void setMaxDbs(std::size_t count) {
        try {
            env_.set_max_dbs(static_cast<MDB_dbi>(count));
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    // sets mapped size in bytes
    void setMapSize(std::size_t size) {
        try {
            env_.set_mapsize(size);
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    // sets increase size to db,
    // space that will be added to current map size
    void setIncreaseSize(std::size_t size) {
        increaseSize_ = size;
    }

    // returns current map size
    size_t mapSize() const {
        if (!isOpen()) {
            return size_t{};
        }

        auto stats = info();
        return stats.me_mapsize;
    }

    // flushes data to drive in sync mode
    void flush() {
        flushImpl(true);
    }

    // flushes data to drive in async mode
    void flushAsync() {
        flushImpl(false);
    }

    // returns database elements count,
    // name - table name at current path, nullptr if only one table exist
    size_t size(const char* name = nullptr) const {
        try {
            auto transaction = lmdb::txn::begin(env_, nullptr, MDB_RDONLY);
            auto dbi = lmdb::dbi::open(transaction, name);

            return dbi.size(transaction);
        }
        catch(const lmdb::error& error) {
            raise(error);
        }

        return size_t{};
    }

    // checks empty database or not
    bool isEmpty() const {
        return size() == 0;
    }

    /// transactions

    // inserts pair of key/value to database as byte stream,
    // name - table name at current path, nullptr if only one table exist,
    // with default flags rewrites value if key exists at db
    void insert(const char* keyData, std::size_t keySize, const char* valueData, std::size_t valueSize,
                const char* name = nullptr,
                const unsigned int flags = lmdb::dbi::default_put_flags) {
        checkMapSize();

        try {
            auto transaction = lmdb::txn::begin(env_);
            auto dbi = lmdb::dbi::open(transaction, name);

            lmdb::val key(reinterpret_cast<const void*>(keyData), keySize);
            lmdb::val value(reinterpret_cast<const void*>(valueData), valueSize);

            dbi.put(transaction, key, value, flags);
            transaction.commit();

            emit commited(keyData, keySize);
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    // inserts any key or value with data/size methods
    template<typename Key, typename Value>
    void insert(const Key& key, const Value& value,
                const char* name = nullptr,
                const unsigned int flags = lmdb::dbi::default_flags) {
        static_assert(!std::is_floating_point_v<Key> && !std::is_floating_point_v<Value>, "Floating point value does not support");

        decltype(auto) k = cast(key);
        decltype(auto) v = cast(value);

        insert(reinterpret_cast<const char*>(k.data()), k.size(),
               reinterpret_cast<const char*>(v.data()), v.size(),
               name, flags);
    }

    // removes key/value pair by key argument as byte stream
    // name - table name at current path, nullptr if only one table exist
    bool remove(const char* data, size_t size, const char* name = nullptr,
                const unsigned int flags = lmdb::dbi::default_flags) {
        try {
            auto transaction = lmdb::txn::begin(env_, nullptr);
            auto dbi = lmdb::dbi::open(transaction, name, flags);

            lmdb::val key(reinterpret_cast<const void*>(data), size);
            const auto result = dbi.del(transaction, key);

            if (result) {
                transaction.commit();
                emit removed(data, size);
            }

            return result;
        }
        catch(const lmdb::error& error) {
            raise(error);
        }

        return false;
    }

    // removes key/value pair by key as data/size method entity
    template<typename Key>
    bool remove(const Key& key, const char* name = nullptr, const unsigned int flags = lmdb::dbi::default_flags) {
        static_assert(!std::is_floating_point_v<Key>, "Floating point value does not support");

        decltype(auto) k = cast(key);
        return remove(reinterpret_cast<const char*>(k.data()), k.size(), name, flags);
    }

    // returns key status at database,
    // name - table name at current path
    bool isKeyExists(const char* data, size_t size, const char* name = nullptr) const {
        try {
            auto transaction = lmdb::txn::begin(env_, nullptr, MDB_RDONLY);
            auto dbi = lmdb::dbi::open(transaction, name);
            auto cursor = lmdb::cursor::open(transaction, dbi);

            lmdb::val key(reinterpret_cast<const void*>(data), size);
            return cursor.get(key, nullptr, MDB_SET);
        }
        catch(const lmdb::error& error) {
            raise(error);
        }

        return false;
    }

    template<typename Key>
    bool isKeyExists(const Key& key, const char* name = nullptr) const {
        static_assert(!std::is_floating_point_v<Key>, "Floating point value does not support");

        decltype(auto) k = cast(key);
        return isKeyExists(reinterpret_cast<const char*>(k.data()), k.size(), name);
    }

    // returns value by key, casts it to template argument
    template<typename T>
    T value(const char* data, size_t size, const char* name = nullptr) const {
        try {
            auto transaction = lmdb::txn::begin(env_, nullptr, MDB_RDONLY);
            auto dbi = lmdb::dbi::open(transaction, name);
            auto cursor = lmdb::cursor::open(transaction, dbi);

            lmdb::val key(reinterpret_cast<const void*>(data), size);
            lmdb::val value;

            const auto result = cursor.get(key, value, MDB_SET);

            if (result) {
                return createResult<T>(value);
            }
        }
        catch(const lmdb::error& error) {
            raise(error);
        }

        return T{};
    }

    // returns and cast to any result with interator consturctor,
    // any key with data/size methods
    template<typename T, typename Key>
    T value(const Key& key) const {
        static_assert(!std::is_floating_point_v<Key>, "Floating point value does not support");

        decltype(auto) k = cast(key);
        return value<T>(reinterpret_cast<const char*>(k.data()), k.size());
    }

    // returns last pair of key/value inserted to database
    template<typename Key, typename Value>
    std::pair<Key, Value> last(const char* name = nullptr) const {
        static_assert(!std::is_floating_point_v<Key> && !std::is_floating_point_v<Value>, "Floating point value does not support");

        try {
            auto transaction = lmdb::txn::begin(env_, nullptr, MDB_RDONLY);
            auto dbi = lmdb::dbi::open(transaction, name);
            auto cursor = lmdb::cursor::open(transaction, dbi);

            lmdb::val key;
            lmdb::val value;

            const auto result = cursor.get(key, value, MDB_LAST);

            if (result) {
                return std::make_pair<Key, Value>(createResult<Key>(key), createResult<Value>(value));
            }
        }
        catch (lmdb::error& error) {
            raise(error);
        }

        return std::make_pair<Key, Value>(Key{}, Value{});
    }

protected:
    void flushImpl(bool force) {
        try {
            env_.sync(force);
            emit flushed();
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    void raise(const lmdb::error& error) const {
        emit failed(LmdbException(error));
    }

    template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    auto cast(const T& value) const {
        static std::array<char, std::numeric_limits<T>::digits10 * 2> bytes{};
        const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);

        if (result.ec != std::errc{}) {
            return std::string_view{};
        }

        return std::string_view(bytes.data(), result.ptr - bytes.data());
    }

    template<typename T, typename = std::enable_if_t<!std::is_integral_v<T>>>
    const T& cast(const T& value) const {
        return value;
    }

    // decays T(&)[size] to const char*
    auto cast(const char* value) const {
        return std::string_view(value, std::strlen(value));
    }

    template<typename T>
    T allocateResult(const char* data, size_t size) const {
        if constexpr (std::is_signed_v<T>) {
            return static_cast<T>(std::stoll(std::string(data, size)));
        }
        else {
            return static_cast<T>(std::stoull(std::string(data, size)));
        }
    }

    template<typename T>
    T createResult(const lmdb::val& value) const {
        if constexpr (IsArray<T>::value) {
            T array;
            assert(array.size() == value.size());
            std::copy(value.data(), value.data() + value.size(), array.data());

            return array;
        }
        else if constexpr (std::is_integral_v<T>) {
            T result = 0;
            const auto res = std::from_chars(value.data(), value.data() + value.size(), result);

            if (res.ec != std::errc{}) {
                return allocateResult<T>(value.data(), value.size());
            }

            return result;
        }
        else if constexpr (std::is_same_v<std::string_view, T>) {
            return std::string_view(value.data(), value.size());
        }
        else {
            return T(value.data(), value.data() + value.size());
        }
    }

    Info info() const {
        Info temp{};
        mdb_env_info(env_.handle(), &temp);
        return temp;
    }

    Stats stats() const {
        Stats temp{};
        mdb_env_stat(env_.handle(), &temp);
        return temp;
    }

    void checkMapSize() {
        Info metaInfo = info();
        Stats metaStats = stats();

        auto freeSpace = metaInfo.me_mapsize - (metaStats.ms_psize * metaInfo.me_last_pgno);

        if (freeSpace < increaseSize_) {
            auto newSize = mapSize() + increaseSize_;
            setMapSize(newSize);

            emit mapSizeIncreased(newSize);
        }
    }

    lmdb::env environment(const unsigned flags) const {
        return lmdb::env::create(flags);
    }

private:
    lmdb::env env_;
    std::string path_;

    bool isOpen_ = false;
    unsigned int flags_;
    size_t increaseSize_ = DefaultIncreaseSize;

public signals:

    // generates when data is written to drive
    FlushSignal flushed;

    // generates when lmdbxx generates an exception
    FailureSignal failed;

    // generates when database data key is commited
    CommitSignal commited;

    // generates when database data key is removed
    RemoveSignal removed;

    // generates when database inseased map size
    IncreaseSignal mapSizeIncreased;
};
}

#endif // LMDBXX_HPP
