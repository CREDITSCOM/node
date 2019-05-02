/**
 * @file user_field.h
 * @author Evgeny V. Zalivochkin
 */

#ifndef _CREDITS_CSDB_USER_FIELD_H_INCLUDED_
#define _CREDITS_CSDB_USER_FIELD_H_INCLUDED_

#include <cinttypes>
#include <string>
#include <type_traits>
#include <vector>

#include "csdb/amount.hpp"
#include "csdb/internal/shared_data.hpp"

namespace csdb {

namespace priv {
class obstream;
class ibstream;
}  // namespace priv

/**
 * Тип идентификатора поля. Тип знаковый. Предполагается, что "стандартные" идентификатор,
 * перечеисленные ниже, отрицательные. "Пользовательские" идентификаторы положительные.
 *
 * Тип дополнительного поля никак не связан с его идентификатором, т.е. дополнительное поле
 * с конкретным идентификатором может быть любого типа, однако в одном объекте не могут
 * быть два поля с одинаковым идетификатором и разными типами (добавление поля с тем
 * же идентификатором замещает предыдущее добавленное значение).
 */
using user_field_id_t = int32_t;

/**
 * @brief Строковый комментарий
 */
constexpr const user_field_id_t UFID_COMMENT = (-1);

/**
 * @brief Класс для хранения значения дополнительного поля.
 *
 * Класс предназначен для храниения типизированного значения дополнительного поля. В настоящий
 * момент поддерживаются три типа значения:
 * - произвольное целое число (::std::is_integral<T>::value == true)
 * - строка (произвольные бинарные данные с длиной, ::std::string)
 * - \ref ::csdb::Amount
 *
 * В классе имеет набор конструкторов для конструирования объекта класса из любого из
 * перечисленных типов, а также шаблон \ref value, позволяющий получать значение поля соответстующего
 * типа. Если тип поля не соответствует запрашиваемому тип, возвращается значени типа по
 * умолчанию (пустая строка для строк, 0 для остальных).
 *
 * Дополнительные поля используются в классах \ref Transaction и \ref Pool.
 */
class UserField {
    SHARED_DATA_CLASS_DECLARE(UserField)
public:
    enum Type : char {
        Unknown = 0,
        Integer = 1,
        String = 2,
        Amount = 3
    };

    template <typename T, typename = typename ::std::enable_if<::std::is_integral<T>::value>::type>
    UserField(T value);
    template <typename T, typename = typename ::std::enable_if<!::std::is_integral<T>::value>::type>
    UserField(const T& value);

    UserField(const char* value);

    bool is_valid() const noexcept;
    Type type() const noexcept;

    template <typename T>
    typename ::std::enable_if<!::std::is_integral<T>::value, T>::type value() const noexcept;

    template <typename T>
    typename ::std::enable_if<::std::is_integral<T>::value, T>::type value() const noexcept;

    bool operator==(const UserField& other) const noexcept;
    inline bool operator!=(const UserField& other) const noexcept;

private:
    void put(::csdb::priv::obstream&) const;
    void put_for_sig(::csdb::priv::obstream&) const;
    bool get(::csdb::priv::ibstream&);
    friend class ::csdb::priv::obstream;
    friend class ::csdb::priv::ibstream;
};

inline bool UserField::operator !=(const UserField& other) const noexcept
{
  return !operator ==(other);
}

template<>
UserField::UserField(uint64_t value);

template<typename T, typename>
inline UserField::UserField(T value) :
  UserField(static_cast<uint64_t>(value))
{}

template<>
inline UserField::UserField(bool value) :
  UserField(static_cast<uint64_t>(value ? 1 : 0))
{}

template<>
uint64_t UserField::value<uint64_t>() const noexcept;

template<>
inline bool UserField::value<bool>() const noexcept
{
  return (0 != value<uint64_t>());
}

template<typename T>
typename ::std::enable_if<::std::is_integral<T>::value, T>::type
inline UserField::value() const noexcept
{
  return static_cast<T>(value<uint64_t>());
}

}  // namespace csdb

#endif // _CREDITS_CSDB_USER_FIELD_H_INCLUDED_
