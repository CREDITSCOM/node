/**
 * @file transaction.h
 * @author Evgeny V. Zalivochkin
 */

#ifndef _CREDITS_CSDB_TRANSACTION_H_INCLUDED_
#define _CREDITS_CSDB_TRANSACTION_H_INCLUDED_

#include <set>

#include <csdb/user_field.hpp>
#include <csdb/internal/shared_data.hpp>
#include <csdb/internal/shared_data_ptr_implementation.hpp>
#include <csdb/internal/types.hpp>

#include <lib/system/common.hpp>

namespace csdb {

namespace priv {
class obstream;
class ibstream;
}  // namespace priv

class Address;
class Amount;
class AmountCommission;
class Currency;
class PoolHash;
class Pool;

/**
 * @brief Unique transaction Id in database
 *
 * Class allow to definitely identify transaction in database. Transaction gets this id only
 *  when it was saved in the pool and and pool is saved into the storage.
 *
 * The content of this class is not strongly specified. Method  \ref Transaction::id is used  
 * to read thransaction id from transaction, and the methods of transforming  to string 
 * (\ref TransactionID::to_string) and obtaining from string (\ref TransactionID::from_string) are supplied.
 *
 * The string format is not strongly specified too, but class guarantee that every string 
 * obtained with the aid of method \ref TransactionID::to_string, will be transformed to 
 * valid Id with the aid of method \ref TransactionID::from_string.
 *
 * The only specified part of this class is tthe ability to obtain
 * \ref cs::Sequence for the pool, where the specified transaction is placed.
 */
class TransactionID {
//    SHARED_DATA_CLASS_DECLARE(TransactionID)
public:
    TransactionID();
    /// \deprecated Constructor will be removed in next versions.
    TransactionID(cs::Sequence poolSeq, cs::Sequence index);

    bool is_valid() const noexcept;
    cs::Sequence pool_seq() const noexcept;

    /// \deprecated Method will be removed in next versions.
    cs::Sequence index() const noexcept;

    std::string to_string() const noexcept;
    cs::Bytes to_byte_stream() const noexcept;

    inline void _update(cs::Sequence pool_seq, cs::Sequence index) {
        pool_seq_ = pool_seq;
        index_ = index;
    }

    /**
     * @brief Obtaining thansaction id from the string representatioin
     * @param[in] str The string representation of transaction Id
     * @return Transaction Id, obtained from string representation.
     *
     * In case when the string representation can't be decoded into
     * transaction id, invalid object is returned
     * (\ref is_valid() == false)
     */
    static TransactionID from_string(const ::std::string& str);

    bool operator==(const TransactionID& other) const noexcept;
    bool operator!=(const TransactionID& other) const noexcept;
    bool operator<(const TransactionID& other) const noexcept;

private:
    void put(::csdb::priv::obstream&) const;
    bool get(::csdb::priv::ibstream&);

    cs::Sequence pool_seq_;
    cs::Sequence index_ = 0;

    friend class ::csdb::priv::obstream;
    friend class ::csdb::priv::ibstream;
    friend class Transaction;
    friend class Pool;
    friend class Storage;
};

class Transaction {
    SHARED_DATA_CLASS_DECLARE(Transaction)

public:
    Transaction(int64_t innerID, Address source, Address target, Currency currency, Amount amount, AmountCommission max_fee, AmountCommission counted_fee,
                const cs::Signature& signature);
    //std::string toSting();

    bool is_valid() const noexcept;
    bool is_read_only() const noexcept;

    TransactionID id() const noexcept;
    int64_t innerID() const noexcept;
    Address source() const noexcept;
    Address target() const noexcept;
    Currency currency() const noexcept;
    Amount amount() const noexcept;
    AmountCommission max_fee() const noexcept;
    AmountCommission counted_fee() const noexcept;
    const cs::Signature& signature() const noexcept;

    void set_innerID(int64_t innerID);
    void set_source(Address source);
    void set_target(Address target);
    void set_currency(Currency currency);
    void set_amount(Amount amount);
    void set_max_fee(AmountCommission max_fee);
    void set_counted_fee(AmountCommission counted_fee);
    void set_counted_fee_unsafe(AmountCommission counted_fee);
    void set_signature(const cs::Signature& signature);
    void update_id(const csdb::TransactionID& id);

    cs::Bytes to_binary();
    static Transaction from_binary(const cs::Bytes& data);

    static Transaction from_byte_stream(const char* data, size_t m_size);
    std::vector<uint8_t> to_byte_stream() const;
    std::vector<uint8_t> to_byte_stream_for_sig() const;

    bool verify_signature(const cs::PublicKey& public_key) const;

    /**
     * @brief Adds user(customized) field to transaction
     * @param[in] id    User field id
     * @param[in] field User field value
     * @return true, if field is added, othewise false
     *
     * Field is added only for transactions not in Read-Only state.
     * (\ref is_read_only == false).
     *
     * If the field with the same identifier was added previously it will be replaces with new one.
     */
    bool add_user_field(user_field_id_t id, UserField field) noexcept;

    /**
     * @brief Returns user (additional field) with id.
     * @param[in] id  user field id
     * @return  Value of user field. If there is no user field with such id in the list
     *          of additional fields, invalid object is returned
     *          (\ref UserField::is_valid == false).
     */
    UserField user_field(user_field_id_t id) const noexcept;

    /**
     * @brief User fields id's list
     * @return  User fields id's list
     */
    ::std::set<user_field_id_t> user_field_ids() const noexcept;

    void set_time(const uint64_t);
    uint64_t get_time() const;

private:
  void put(::csdb::priv::obstream&) const;
  bool get(::csdb::priv::ibstream&);
  friend class ::csdb::priv::obstream;
  friend class ::csdb::priv::ibstream;
  friend class Pool;
};

}  // namespace csdb

#endif // _CREDITS_CSDB_TRANSACTION_H_INCLUDED_
