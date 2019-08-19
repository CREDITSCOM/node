/**
 * @file wallet.h
 * @author Roman Bukin
 */

#ifndef _CREDITS_CSDB_WALLET_H_INCLUDED_
#define _CREDITS_CSDB_WALLET_H_INCLUDED_

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

}  // namespace csdb

#endif  // _CREDITS_CSDB_WALLET_H_INCLUDED_
