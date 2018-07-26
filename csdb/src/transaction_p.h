/**
  * @file transaction_p.h
  * @author Evgeny V. Zalivochkin
  */

#pragma once
#ifndef _CREDITS_CSDB_TRANSACTION_PRIVATE_H_INCLUDED_
#define _CREDITS_CSDB_TRANSACTION_PRIVATE_H_INCLUDED_

#include <map>

#include "csdb/internal/shared_data_ptr_implementation.h"

#include "csdb/transaction.h"

#include "csdb/address.h"
#include "csdb/amount.h"
#include "csdb/currency.h"
#include "csdb/pool.h"

namespace csdb {

class TransactionID::priv : public ::csdb::internal::shared_data
{
  inline priv() :
    index_(0)
  {}

  inline priv(PoolHash pool_hash, TransactionID::sequence_t index) :
    pool_hash_(pool_hash),
    index_(index)
  {}

  inline void _update(PoolHash pool_hash, TransactionID::sequence_t index)
  {
    pool_hash_ = pool_hash;
    index_ = index;
  }

  PoolHash pool_hash_;
  TransactionID::sequence_t index_ = 0;
  friend class TransactionID;
  friend class Transaction;
  friend class Pool;
};

class Transaction::priv : public ::csdb::internal::shared_data
{
  inline priv() :
    read_only_(false),
    amount_(0_c),
    balance_(0_c)
  {}

  inline priv(const priv& other) :
    read_only_(false),
    source_(other.source_),
    target_(other.target_),
    currency_(other.currency_),
    amount_(other.amount_),
    balance_(other.balance_),
    user_fields_(other.user_fields_)
  {}

  inline priv(Address source, Address target, Currency currency, Amount amount, Amount balance) :
    read_only_(false),
    source_(source),
    target_(target),
    currency_(currency),
    amount_(amount),
    balance_(balance)
  {}

  inline void _update_id(PoolHash pool_hash, TransactionID::sequence_t index)
  {
    id_.d->_update(pool_hash, index);
    read_only_ = true;
  }

  bool read_only_;
  TransactionID id_;
  Address source_;
  Address target_;
  Currency currency_;
  Amount amount_;
  Amount balance_;
  ::std::map<::csdb::user_field_id_t, ::csdb::UserField> user_fields_;

  friend class Transaction;
  friend class Pool;
  friend class ::csdb::internal::shared_data_ptr<priv>;
};

} // namespace csdb

#endif // _CREDITS_CSDB_TRANSACTION_PRIVATE_H_INCLUDED_
