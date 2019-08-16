/**
 * @file currency.h
 * @author Evgeny V. Zalivochkin
 */

#ifndef _CREDITS_CSDB_CURRENCY_H_INCLUDED_
#define _CREDITS_CSDB_CURRENCY_H_INCLUDED_

#include <string>
#include <vector>

#include <csdb/internal/shared_data.hpp>
#include <csdb/internal/shared_data_ptr_implementation.hpp>

namespace csdb {

namespace priv {
class obstream;
class ibstream;
}  // namespace priv

class Currency {
    SHARED_DATA_CLASS_DECLARE(Currency)
public:
    Currency(const uint8_t& id);

    bool is_valid() const noexcept;
    std::string to_string() const noexcept;

    bool operator==(const Currency& other) const noexcept;
    bool operator!=(const Currency& other) const noexcept;
    bool operator<(const Currency& other) const noexcept;

private:
    void put(::csdb::priv::obstream&) const;
    bool get(::csdb::priv::ibstream&);
    friend class ::csdb::priv::obstream;
    friend class ::csdb::priv::ibstream;
};

typedef std::vector<Currency> CurrencyList;

}  // namespace csdb

#endif  // _CREDITS_CSDB_CURRENCY_H_INCLUDED_
