/**
 * @file pool.h
 * @author Evgeny V. Zalivochkin
 */

#ifndef _CREDITS_CSDB_POOL_H_INCLUDED_
#define _CREDITS_CSDB_POOL_H_INCLUDED_

#include <array>
#include <cinttypes>
#include <climits>
#include <string>
#include <vector>

#include <boost/functional/hash.hpp>

#include <csdb/address.hpp>
#include <csdb/internal/shared_data.hpp>
#include <csdb/internal/shared_data_ptr_implementation.hpp>
#include <csdb/internal/types.hpp>
#include <csdb/storage.hpp>
#include <csdb/transaction.hpp>
#include <csdb/user_field.hpp>

#include <lib/system/common.hpp>
#include <cscrypto/cscrypto.hpp>

namespace csdb {

class Transaction;
class TransactionID;

namespace priv {
class obstream;
class ibstream;
}  // namespace priv

class PoolHash {
    SHARED_DATA_CLASS_DECLARE(PoolHash)

public:
    bool is_empty() const noexcept;
    size_t size() const noexcept;
    std::string to_string() const noexcept;

    /**
     * @brief Converting hash from hex string format
     * @param[in] str String value of hash
     * @return Hash in format PoolHash, obtained from string value
     *
     * In case when the string value is incorrect empty hash is returned.
     */
    static PoolHash from_string(const ::std::string& str);

    cs::Bytes to_binary() const noexcept;
    static PoolHash from_binary(cs::Bytes&& data);

    bool operator==(const PoolHash& other) const noexcept;
    inline bool operator!=(const PoolHash& other) const noexcept;

    /**
     * @brief operator <
     *
     * Operator for sorting the class containers or for using class as key.
     */
    bool operator<(const PoolHash& other) const noexcept;
    size_t calcHash() const noexcept;

    static PoolHash calc_from_data(const cs::Bytes& data);

private:
    void put(::csdb::priv::obstream&) const;
    bool get(::csdb::priv::ibstream&);
    friend class ::csdb::priv::obstream;
    friend class ::csdb::priv::ibstream;
    friend class Storage;
};

class PoolHash::priv : public ::csdb::internal::shared_data {
public:
    cs::Bytes value;
    DEFAULT_PRIV_CLONE();
};
SHARED_DATA_CLASS_IMPLEMENTATION_INLINE(PoolHash)

class Pool {
    SHARED_DATA_CLASS_DECLARE(Pool)
public:

    using Transactions = std::vector<csdb::Transaction>;
    class NewWalletInfo {
    public:
        using WalletId = csdb::internal::WalletId;

        enum AddressType
        {
            AddressIsSource,
            AddressIsTarget
        };

        struct AddressId {
            size_t trxInd_ : sizeof(size_t) * CHAR_BIT - 1;
            size_t addressType_ : 1;

            bool operator==(const AddressId& rh) const {
                return trxInd_ == rh.trxInd_ && addressType_ == rh.addressType_;
            }
            bool operator!=(const AddressId& rh) const {
                return !operator==(rh);
            }
        };

    public:
        NewWalletInfo()
        : addressId_()
        , walletId_() {
        }
        NewWalletInfo(AddressId addressId, csdb::internal::WalletId walletId)
        : addressId_(addressId)
        , walletId_(walletId) {
        }
        void put(::csdb::priv::obstream&) const;
        bool get(::csdb::priv::ibstream&);

        bool operator==(const NewWalletInfo& rh) const {
            return addressId_ == rh.addressId_ && walletId_ == rh.walletId_;
        }
        bool operator!=(const NewWalletInfo& rh) const {
            return !operator==(rh);
        }

    public:
        AddressId addressId_;
        WalletId walletId_;
    };

    struct SmartSignature {
        cs::PublicKey smartKey;
        cs::Sequence smartConsensusPool;
        cs::BlockSignatures signatures;
    };

    using NewWallets = std::vector<NewWalletInfo>;

public:
    Pool(PoolHash previous_hash, cs::Sequence sequence, const Storage& storage = Storage());

    static PoolHash hash_from_binary(cs::Bytes&& data);
    static Pool from_binary(cs::Bytes&& data, bool makeReadOnly = true);
    static Pool meta_from_binary(cs::Bytes&& data, size_t& cnt);
    static Pool load(const PoolHash& hash, Storage storage = Storage());

    // static Pool from_byte_stream(const char* data, size_t size);
    char* to_byte_stream(uint32_t&);
    cs::Bytes to_byte_stream_for_sig();
    cs::Bytes to_binary_updated() const;

    Pool meta_from_byte_stream(const char*, size_t);
    static Pool from_lz4_byte_stream(size_t);

    bool is_valid() const noexcept;
    bool is_read_only() const noexcept;
    uint8_t version() const noexcept;
    PoolHash previous_hash() const noexcept;
    cs::Sequence sequence() const noexcept;
    Storage storage() const noexcept;
    size_t transactions_count() const noexcept;
    const cs::PublicKey& writer_public_key() const noexcept;
    const std::vector<cs::PublicKey>& confidants() const noexcept;
    const std::vector<cs::Signature>& signatures() const noexcept;
    const ::std::vector<SmartSignature>& smartSignatures() const noexcept;
    const csdb::Amount& roundCost() const noexcept;
    const std::vector<cs::Signature>& roundConfirmations() const noexcept;
    size_t hashingLength() const noexcept;

    void set_version(uint8_t version) noexcept;
    void set_previous_hash(PoolHash previous_hash) noexcept;
    void set_sequence(cs::Sequence sequence) noexcept;
    void set_storage(const Storage& storage) noexcept;
    void set_confidants(const std::vector<cs::PublicKey>& confidants) noexcept;
    void set_signatures(std::vector<cs::Signature>& blockSignatures) noexcept;
    void add_smart_signature(const SmartSignature& smartSignature) noexcept;
    void add_real_trusted(const uint64_t trustedMask) noexcept;
    void add_confirmation_mask(const uint64_t confMask) noexcept;
    void add_number_trusted(const uint8_t trustedMask) noexcept;
    void add_number_confirmations(const uint8_t confMask) noexcept;
    void setRoundCost(const csdb::Amount& roundCost) noexcept;
    void add_round_confirmations(const std::vector<cs::Signature>& confirmations) noexcept;

    Transactions& transactions();
    const Transactions& transactions() const;

    NewWallets* newWallets() noexcept;
    const NewWallets& newWallets() const noexcept;
    bool getWalletAddress(const NewWalletInfo& info, csdb::Address& wallAddress) const;
    uint64_t realTrusted() const noexcept;
    uint64_t roundConfirmationMask() const noexcept;
    uint8_t numberConfirmations() const noexcept;
    uint8_t numberTrusted() const noexcept;
    /**
     * @brief Adds transaction to pool.
     * @param[in] transaction transaction to be added
     * @return true, if transaction addad successfully. false, if transaction didn't pass the check
     *
     * The transaction can be added only to new pool (i.e. only if \ref is_read_only is false).
     *
     * Before adding the transaction should be checked for validity in database, set for its pool, 
     * and through previously added transactions. If database is not set, or it's closed the 
     * check is not successfull.
     */
    bool add_transaction(Transaction transaction
#ifdef CSDB_UNIT_TEST
                         ,
                         bool skip_check
#endif
    );

    /**
     * @brief Finalize the pool creation.
     * @return true, if the binary representation is formed for the current pool
     *
     * For the new-created pool (i.e. if \ref is_read_only returns false) creates its binary 
     * representation, calculates hash and switches pool into read-only state. After that
     * the methods \ref hash, \ref save and \ref to_binary are available for the pool.
     *
     * For read-only pools this method doesn't process anything and just returns true.
     */
    bool compose();

    /**
     * @brief Pools hash
     * @return Pools hash if pool is in the read-only state, and empty hash in other cases.
     */
    PoolHash hash() const noexcept;
    void recount() noexcept;

    /**
     * @brief Pools binary representation
     * @return Pools binary representation, if pool is in the read-only state and empty bytes-vector in other cases.
     */
    cs::Bytes to_binary() const noexcept;

    /**
     * @brief Saving pool in storage
     * @param[in] storage Storage to save the pool
     * @return  true, if save-operation succeded.
     *
     * Function works only for finalized pools, (i.e. those are in read-only state).
     *
     * If the supplied storage is inavailable (not opened), then method tries to save pool in 
     * the storage received during constructing time. If this storage is inavailable too,
     * method tries to use default storage. If none of those storages is available, FALSE is returned. 
     *
     * Function doesn't check if there is a pool with similar hash in the storage, because the possibility
     * of equality two different pool's hashes is extremely low. 
     *
     * If the saving procedure was successful, then the storage, where pool was saved becomes the storage 
     * for the objects item (thats why the method isn't constant).
     */
    bool save(Storage storage = Storage());

    /**
     * @brief adds additional information fiels to pool
     * @param[in] id    - user field Id
     * @param[in] field - user field value 
     * @return true, if field is added, otherwise false
     *
     * The field can be added only to pools, those are not in Read-Only state
     * (if \ref is_read_only returns false).
     *
     * If the field with the same Id is added before it will be replace by new one.
     */
    bool add_user_field(user_field_id_t id, const UserField& field) noexcept;

    /**
     * @brief Returns user field (additional customized field).
     * @param[in] id  User field Id
     * @return  The value of user field. If there is no field with such Id
     *          in the user field's list, invalid object is returned 
     *          (\ref UserField::is_valid == false).
     */
    UserField user_field(user_field_id_t id) const noexcept;

    /**
     * @brief List of user field's Ids
     * @return  List of user field's Ids
     */
    ::std::set<user_field_id_t> user_field_ids() const noexcept;

    /// \deprecated Method will be deprecated in later versions of csdb.
    Transaction transaction(size_t index) const;

    /**
     * @brief Get transaction using transaction Id
     * @param[in] id - transaction identifier (pool_number.transaction_number)
     * @return Returns transaction object. If transaction doesn't exist in current pool, r
     *         eturns invalid object (\ref ::csdb::Transaction::is_valid() == false).
     */
    Transaction transaction(TransactionID id) const;

    /**
     * @brief Get last transaction using source address
     * @param[in] source - source address
     * @return transaction object. If transaction doesn't exist in current pool, returns
     *         invalid object (\ref ::csdb::Transaction::is_valid() == false).
     */
    Transaction get_last_by_source(const Address& source) const noexcept;

    /**
     * @brief Get last transaction using target address
     * @param[in] target - target address
     * @return transaction object. If transaction doesn't exist in current pool, returns
     *         invalid object (\ref ::csdb::Transaction::is_valid() == false).
     */
    Transaction get_last_by_target(const Address& target) const noexcept;

    friend class Storage;
};

inline bool PoolHash::operator !=(const PoolHash &other) const noexcept
{
  return !operator ==(other);
}
}  // namespace csdb

namespace boost {
template <>
class hash<csdb::PoolHash> {
public:
    size_t operator()(const csdb::PoolHash &obj) const {
        return obj.calcHash();
    }
};
}  // namespace boost

#endif // _CREDITS_CSDB_POOL_H_INCLUDED_
