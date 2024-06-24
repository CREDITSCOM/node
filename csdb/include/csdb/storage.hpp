/**
 * @file storage.h
 * @author Roman Bukin, Evgeny Zalivochkin
 */

#ifndef _CREDITS_CSDB_STORAGE_H_INCLUDED_
#define _CREDITS_CSDB_STORAGE_H_INCLUDED_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <csdb/database.hpp>
#include <csdb/transaction.hpp>
#include <csdb/internal/shared_data_ptr_implementation.hpp>

#include <lib/system/common.hpp>
#include <lib/system/signals.hpp>

namespace csdb {

class Pool;
class PoolHash;
class Address;
class Wallet;
class Transaction;
class TransactionID;

/** @brief The read block signal, caller may assign test_failed to true if block is logically corrupted */
using ReadBlockSignal = cs::Signal<void(const csdb::Pool& block, bool* test_failed)>;

using BlockReadingStartedSingal = cs::Signal<void(cs::Sequence lastBlockNum)>;
using BlockReadingStoppedSignal = cs::Signal<void()>;

/**
 * @brief Storage object
 *
 * Storage object is a class, that is used to process all kinds of actions with data in 
 * database(storage) (read, save and so on). Object is a shared pointer(shared_ptr),
 * and can be freely copied, but all copies are linked to one (the same) storage object,
 * so the changes, made in one object are translated in all copies.
 *
 * To work with physical storage the interface class \ref ::csdb::Database is used.
 *
 * (orig)=== In previous version there was only one realization of interface \ref ::csdb::Database -
 * \ref ::csdb::DatabaseLevelDB. It's used to open the object \ref ::csdb::Storage too. ===
 * 
 * In current version this interface was changed for berkly db, that showed more reliable results under increased loads.
 * 
 * When creating \ref ::csdb::Storage object has not opened status, and calls of any method reading or 
 * saving data spawn exception ::csdb::Storage::NotOpen. To work with object \ref ::csdb::Storage
 * the method \ref ::csdb::Storage::open should be called or the object should be created with method
 * ::csdb::Storage::get.
 */
class Storage final {
private:
    class priv;

    bool write_queue_search(const PoolHash& hash, Pool& res_pool) const;
    bool write_queue_pop(Pool& res_pool);

public:
    using WeakPtr = ::std::weak_ptr<priv>;

    enum Error {
        NoError = 0,
        NotOpen = 1,
        DatabaseError = 2,
        ChainError = 3,
        DataIntegrityError = 4,
        UserCancelled = 5,
        InvalidParameter = 6,
        UnknownError = 255,
    };

public:
    Error last_error() const;
    std::string last_error_message() const;
    Database::Error db_last_error() const;
    std::string db_last_error_message() const;

public:
    Storage();
    ~Storage();

public:
    Storage(const Storage&) = default;
    Storage(Storage&&) = default;
    Storage& operator=(const Storage&) = default;
    Storage& operator=(Storage&&) = default;

    explicit Storage(WeakPtr ptr) noexcept;
    WeakPtr weak_ptr() const noexcept;

public:
    struct OpenOptions {
        /// Db driver unit
        ::std::shared_ptr<Database> db;
        ::cs::Sequence newBlockchainTop = ::cs::kWrongSequence;
        ::cs::Sequence startSequence = 0;
    };

    struct OpenProgress {
        uint64_t poolsProcessed;
    };

    /**
     * @brief Callback for open operation
     * @return true, if the operation should be interrupted.
     */
    typedef ::std::function<bool(const OpenProgress&)> OpenCallback;

    /**
     * @brief Opens storage with parameters.
     * @param opt       Parameter's set for opening storage. Structure \ref OpenOptions::db member has to
     *                  point at alreadyy opened database.
     * @param callback  Callback for opening
     * @return          true, if successfull opening and check, otherwise false.
     *
     * In case of fail the error info can be obtained with the aid of methods \ref last_error,
     * \ref last_error_message, \ref db_last_error() and \ref db_last_error_message()
     */
    bool open(const OpenOptions& opt, OpenCallback callback = nullptr);

    /**
     * @brief Opens storage using given path
     * @param path_to_base  path to database (terminating slash is not necessary)
     * @param callback      Callback to open
     * @return              true, if opening was successfull, otherwise false.
     * @overload
     *
     * Method tries to open the existing or to create a new storage with database driver
     * defined for current platform. If the given path does not exist, method tries to
     * create the new path. If the given path is empty, then the default path for current platform will be used.
     *
     * In case of fail the error info can be received with methods:  \ref last_error,
     * \ref last_error_message, \ref db_last_error() and \ref db_last_error_message()
     */
    bool open(const ::std::string& path_to_base = ::std::string{},
              OpenCallback callback = nullptr,
              cs::Sequence newBlockchainTop = cs::kWrongSequence,
              cs::Sequence startReadFrom = 0);

    /**
     * @brief Creating the storage using the parameters set.
     *
     * Creates a storage and tries to open it. See \ref open(const OpenOptions &opt, OpenCallback callback);
     */
    static inline Storage get(const OpenOptions& opt, OpenCallback callback = nullptr);

    /**
     * @brief Creating the storage st the given path.
     *
     * Creates a storage and tries to open it.
     * See \ref open(const ::std::string& path_to_base, OpenCallback callback);
     */
    static inline Storage get(const ::std::string& path_to_base = ::std::string{}, OpenCallback callback = nullptr);

    /**
     * @brief Checks if the storage is opened.
     * @return true, if the storage is opened and can be used, otherwise false.
     */
    bool isOpen() const;

    /**
     * @brief Closes the storage
     *
     * After using this method every reading or saving info methods leads to error \ref NotOpen.
     */
    void close();

    /**
     * @brief Last block hash
     * @return Last block hash
     *
     * Last block hash is returned correctly only in that case if the blocks, 
     * saved in the storage (event during db opening), are the corrent consistent chain.
     *
     * If storage is empty or doesn't has the correctly finished chain, returns empty hash.
     */
    PoolHash last_hash() const noexcept;
    void set_last_hash(const PoolHash&) noexcept;
    void set_size(const size_t) noexcept;

    /**
     * @brief writes the pool into the storage
     * @param[in] pool Pool to be stored
     * @return true, if poll stored.
     *
     * \sa ::csdb::Pool::save
     */
    bool pool_save(Pool pool);

    /**
     * @brief Loads pool form the storage/ overloaded method
     * @param[in]   hash - hash of pool to be loaded,
     *              sequence - sequence of pool to be loaded.
     * @return Loaded pool. If pool was not found or the storage data can't be
     *         interpreted, as pool, the invalid pool is returned. 
     *         (\ref ::csdb::Pool::is_valid() == false).
     *
     * \sa ::csdb::Pool::load
     */
    Pool pool_load(const PoolHash& hash) const;
    Pool pool_load(const cs::Sequence sequence) const;
    Pool pool_load_meta(const PoolHash& hash, size_t& cnt) const;

    Pool pool_remove_last();
    /**
	 * @brief Removes last pool form the storage without calculating pool hash. Used in case of data consistancy interrruption,
	 * when hash can't be obtained from the deleted pool, but the hash value is known form another source, i.e. cache. 
	 * Method can correctly delete the last pool when its hash is deleted from the hash table "hash -> sequence", and the pool 
     * is still not deleted from the pools table.
	 * @param[in] test_sequence Pool sequence (block number) to be checked. Sequence should exactly point the last pool in the storage.
	 * @param[in] test_hash Pools hash, that should be removed. Should point the last pool in the storage or not point the record int the
	 * "hash - sequence" table at all. The lase case is possible after previously not correct last pool removing procedure
	 * @return true if pool is successfully removed
	 *
	 * \sa ::csdb::Pool::load
	 */bool pool_remove_last_repair(cs::Sequence test_sequence, const csdb::PoolHash& test_hash);

     bool pool_remove_(cs::Sequence testSequence);

    /**
     * @brief Requesting the transaction using transaction id (pool.number_in_pool).
     * @param[in] id transaction id
     * @return Transaction object. If transaction with such id can't be found in the storage,
     *         returns invalid object(\ref ::csdb::Transaction::is_valid() == false).
     */
    Transaction transaction(const TransactionID& id) const;

    /**
     * @brief Get last transaction in storage from sender using its address.
     * @param[in] source Senders address
     * @return Transaction object. If transaction with such id can't be found in the storage,
     *         returns invalid object(\ref ::csdb::Transaction::is_valid() == false).
     */
    Transaction get_last_by_source(Address source) const noexcept;

    /**
     * @brief Get last transaction in storage to recipient using its address.
     * @param[in] target Recipient address
     * @return Transaction object. If transaction with such id can't be found in the storage,
     *         returns invalid object(\ref ::csdb::Transaction::is_valid() == false).
     */
    Transaction get_last_by_target(Address target) const noexcept;

    /**
     * @brief size returns number of pools in the storage
     * @return number of pools in the storage
     *
     * \deprecated method will be removed in the next versions
     */
    size_t size() const noexcept;

    /**
     * @brief wallet get wallet using address
     * Wallet contains all data cor calculating the balance of the address
     * @param addr wallet address
     * @return wallet
     * 
     * \deprecated method will be removed in the next versions, because don't support 
     * different porpose transaction functionality. Use wallet cache instead 
     */
    Wallet wallet(const Address& addr) const;

    /**
     * @brief transactions - get transaction's list for given address
     * @param addr wallet address
     * @param limit maximum number of transactions in the list (limited by 100)
     * @param offset transaction Id after that the list will be started
     * @return transaction's list
     *
     * \deprecated method will be removed in the next versions
     */
    std::vector<Transaction> transactions(const Address& addr, size_t limit = 100, const TransactionID& offset = TransactionID()) const;

    /**
     * Gets contract data from storage.
     *
     * @author  0xAAE
     * @date    19.06.2019
     *
     * @param           abs_addr   The contract address.
     * @param [in,out]  data       The contract data.
     *
     * @returns True if it succeeds, false if it fails.
     */

    bool get_contract_data(const Address& abs_addr /*input*/, cs::Bytes& data /*output*/) const;

    /**
     * Updates the contract data in storage.
     *
     * @author  0xAAE
     * @date    19.06.2019
     *
     * @param   abs_addr   The contract address.
     * @param   data       The contract data.
     *
     * @returns True if it succeeds, false if it fails.
     */

    bool update_contract_data(const Address& abs_addr /*input*/, const cs::Bytes& data /*input*/) const;

    /**
     * Gets from database pool sequence by pool hash
     *
     * @param   hash    The pool hash.
     *
     * @returns Pool sequence of type cs::Sequence. If hash is not found in database returns std::numeric_limits<cs::Sequence>::max() value
     */

    cs::Sequence pool_sequence(const PoolHash& hash) const;

    csdb::PoolHash pool_hash(cs::Sequence sequence) const;

public signals:
    const ReadBlockSignal& readBlockEvent() const;
    const BlockReadingStartedSingal& readingStartedEvent() const;
    const BlockReadingStoppedSignal& readingStoppedEvent() const;

private:
  Pool pool_load_internal(const PoolHash& hash, const bool metaOnly, size_t& trxCnt) const;

  ::std::shared_ptr<priv> d;
};

inline Storage Storage::get(const OpenOptions &opt, OpenCallback callback)
{
  Storage s;
  s.open(opt, callback);
  return s;
}

inline Storage Storage::get(const std::string& path_to_base, OpenCallback callback)
{
  Storage s;
  s.open(path_to_base, callback);
  return s;
}

}  // namespace csdb

#endif // _CREDITS_CSDB_STORAGE_H_INCLUDED_
