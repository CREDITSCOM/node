/**
 * @file wallet.h
 * @author Roman Bukin
 */

#ifndef _CREDITS_CSDB_WALLET_H_INCLUDED_
#define _CREDITS_CSDB_WALLET_H_INCLUDED_

#include <csdb/address.hpp>
#include <csdb/currency.hpp>
#include <csdb/internal/shared_data.hpp>
#include <csdb/internal/shared_data_ptr_implementation.hpp>
#include <csdb/storage.hpp>

namespace csdb {

class Address;
class Amount;
class Storage;

class Wallet {
    SHARED_DATA_CLASS_DECLARE(Wallet)

public:
    static Wallet get(Address address, Storage storage = Storage());

    bool is_valid() const noexcept;
    Address address() const noexcept;

    CurrencyList currencies() const noexcept;
    Amount amount(Currency currency) const noexcept;
};

class Wallet::priv : public ::csdb::internal::shared_data {
    priv() = default;

    explicit priv(Address address)
    : address_(address) {
    }

    Address address_;
    std::map<Currency, Amount> amounts_;

    priv clone() const {
        priv result;

        result.address_ = address_.clone();

        for (auto &am : amounts_)
            result.amounts_[am.first.clone()] = am.second;

        return result;
    }

    friend class Wallet;
};
SHARED_DATA_CLASS_IMPLEMENTATION_INLINE(Wallet)

}  // namespace csdb

#endif  // _CREDITS_CSDB_WALLET_H_INCLUDED_
