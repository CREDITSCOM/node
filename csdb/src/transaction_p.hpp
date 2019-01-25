/**
 * @file transaction_p.h
 * @author Evgeny V. Zalivochkin
 */

#pragma once
#ifndef _CREDITS_CSDB_TRANSACTION_PRIVATE_H_INCLUDED_
#define _CREDITS_CSDB_TRANSACTION_PRIVATE_H_INCLUDED_

#include <map>

#include "csdb/internal/shared_data_ptr_implementation.hpp"

#include "csdb/transaction.hpp"

#include "csdb/address.hpp"
#include "csdb/amount.hpp"
#include "csdb/amount_commission.hpp"
#include "csdb/currency.hpp"
#include "csdb/pool.hpp"

namespace csdb {

class TransactionID::priv : public ::csdb::internal::shared_data {
  inline priv()
  : index_(0) {
  }

  inline priv(PoolHash pool_hash, TransactionID::sequence_t index)
  : pool_hash_(pool_hash)
  , index_(index) {
  }

  inline void _update(PoolHash pool_hash, TransactionID::sequence_t index) {
    pool_hash_ = pool_hash;
    index_ = index;
  }

  priv clone() const {
    priv result;
    result.pool_hash_ = pool_hash_.clone();
    result.index_ = index_;
    return result;
  }

  PoolHash pool_hash_;
  TransactionID::sequence_t index_ = 0;
  friend class TransactionID;
  friend class Transaction;
  friend class Pool;
};

class Transaction::priv : public ::csdb::internal::shared_data {
  inline priv()
  : read_only_(false)
  , amount_(0_c)
  , signature_() {
  }

  inline priv(const priv& other)
  : ::csdb::internal::shared_data()
  , read_only_(other.read_only_)
  , id_(other.id_)
  , innerID_(other.innerID_)
  , source_(other.source_)
  , target_(other.target_)
  , currency_(other.currency_)
  , amount_(other.amount_)
  , max_fee_(other.max_fee_)
  , counted_fee_(other.counted_fee_)
  , signature_(other.signature_)
  , user_fields_(other.user_fields_)
  , time_(other.time_) {
  }

  inline priv(int64_t innerID, Address source, Address target, Currency currency, Amount amount,
              AmountCommission max_fee, AmountCommission counted_fee, std::string signature)
  : read_only_(false)
  , innerID_(innerID)
  , source_(source)
  , target_(target)
  , currency_(currency)
  , amount_(amount)
  , max_fee_(max_fee)
  , counted_fee_(counted_fee)
  , signature_(signature) {
  }

  inline void _update_id(PoolHash pool_hash, TransactionID::sequence_t index) {
    id_.d->_update(pool_hash, index);
    read_only_ = true;
  }

  priv clone() const {
    priv result;
    result.read_only_ = read_only_;
    result.id_ = id_.clone();
    result.innerID_ = innerID_;

    result.source_ = source_.clone();
    result.target_ = target_.clone();
    result.currency_ = currency_.clone();
    result.amount_ = amount_;
    result.max_fee_ = max_fee_;
    result.counted_fee_ = counted_fee_;
    result.signature_ = signature_;

    for (auto& uf : user_fields_)
      result.user_fields_[uf.first] = uf.second.clone();

    result.time_ = time_;

    return result;
  }

  bool read_only_;
  TransactionID id_;
  int64_t innerID_;
  Address source_;
  Address target_;
  Currency currency_;
  Amount amount_;
  AmountCommission max_fee_;
  AmountCommission counted_fee_;
  std::string signature_;
  ::std::map<::csdb::user_field_id_t, ::csdb::UserField> user_fields_;

  uint64_t time_{};  // optional, not set automatically

  friend class Transaction;
  friend class Pool;
  friend class ::csdb::internal::shared_data_ptr<priv>;
};

}  // namespace csdb

#endif  // _CREDITS_CSDB_TRANSACTION_PRIVATE_H_INCLUDED_
