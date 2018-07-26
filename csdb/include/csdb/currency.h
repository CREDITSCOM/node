/**
  * @file currency.h
  * @author Evgeny V. Zalivochkin
  */

#pragma once
#ifndef _CREDITS_CSDB_CURRENCY_H_INCLUDED_
#define _CREDITS_CSDB_CURRENCY_H_INCLUDED_

#include "csdb/internal/shared_data.h"
#include <string>
#include <vector>

namespace csdb {

namespace priv {
class obstream;
class ibstream;
} // namespace priv

class Currency
{
  SHARED_DATA_CLASS_DECLARE(Currency)
public:
  Currency(const std::string &name);

  bool is_valid() const noexcept;
  std::string to_string() const noexcept;

  bool operator ==(const Currency& other) const noexcept;
  bool operator !=(const Currency& other) const noexcept;
  bool operator < (const Currency& other) const noexcept;

private:
  void put(::csdb::priv::obstream&) const;
  bool get(::csdb::priv::ibstream&);
  friend class ::csdb::priv::obstream;
  friend class ::csdb::priv::ibstream;
};

typedef std::vector<Currency> CurrencyList;

} // namespace csdb

#endif // _CREDITS_CSDB_CURRENCY_H_INCLUDED_
