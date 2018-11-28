#include "csdb/wallet.hpp"
#include <map>

#include "csdb/address.hpp"
#include "csdb/amount.hpp"
#include "csdb/csdb.hpp"
#include "csdb/internal/shared_data_ptr_implementation.hpp"
#include "csdb/pool.hpp"

namespace csdb {

class Wallet::priv : public ::csdb::internal::shared_data {
  priv() = default;

  explicit priv(Address address)
  : address_(address) {
  }

  Address address_;
  std::map<Currency, Amount> amounts_;

  friend class Wallet;
};
SHARED_DATA_CLASS_IMPLEMENTATION(Wallet)

bool Wallet::is_valid() const noexcept {
  return d->address_.is_valid();
}

Address Wallet::address() const noexcept {
  return d->address_;
}

CurrencyList Wallet::currencies() const noexcept {
  CurrencyList res;
  res.reserve(d->amounts_.size());

  for (const auto &it : d->amounts_) {
    res.push_back(it.first);
  }

  return res;
}

Amount Wallet::amount(Currency currency) const noexcept {
  const auto it = d->amounts_.find(currency);
  return (it != d->amounts_.end()) ? it->second : 0_c;
}

Wallet Wallet::get(Address address, Storage storage) {
  if (!storage.isOpen()) {
    storage = csdb::defaultStorage();
    if (!storage.isOpen()) {
      return Wallet{};
    }
  }
  priv *d = new priv(address);

  for (Pool pool = Pool::load(storage.last_hash(), storage); pool.is_valid();
       pool = Pool::load(pool.previous_hash(), storage)) {
    for (size_t i = 0; i < pool.transactions_count(); ++i) {
      const Transaction t = pool.transaction(i);
      const Currency currency = t.currency();
      if (t.source() == address) {
        d->amounts_[currency] -= t.amount();
      }
      if (t.target() == address) {
        d->amounts_[currency] += t.amount();
      }
    }
  }

  return Wallet(d);
}

}  // namespace csdb
