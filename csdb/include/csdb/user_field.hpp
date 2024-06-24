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

#include <csdb/amount.hpp>
#include <csdb/internal/shared_data.hpp>
#include <csdb/internal/shared_data_ptr_implementation.hpp>

namespace csdb {

namespace priv {
class obstream;
class ibstream;
}  // namespace priv

/**
 * User field Id type. Types is signed. It's assumed that "standard" ids. 
 * Mentioned later are negative. "User" ids are positive.
 *
 * The type of user field is not connected with its id, i.e. user field
 * with predefined id can be of different type, but in the one object unit there is not possible
 * the existance of two user fields with the same id but different types. 
 * The addition of user field with the same id replaces thee former one.
 */
using user_field_id_t = int32_t;

/**
 * @brief comment
 */
constexpr const user_field_id_t UFID_COMMENT = (-1);

/**
 * @brief Class for additional field holding
 *
 * The purpose of this class is to keep the predefined type value of additional field.  In current 
 * version three types of user filed are supperted:
 * - integer value (::std::is_integral<T>::value == true)
 * - string value (different binary data, ::std::string)
 * - \ref ::csdb::Amount
 *
 * Class has the set of constructors fro creating the objects of class form one of mentioned 
 * above types, and the template \ref value, that allows to get value of the field of corresponding type
 * IF the type does not correspond to requested one, the default type value will be returned 
 * (empty string for string, or 0 for other).
 *
 * User fields are used in classes \ref Transaction and \ref Pool.
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

class UserField::priv : public ::csdb::internal::shared_data {
    inline priv()
    : type_(UserField::Unknown)
    , i_value_(0) {
    }

    inline priv(uint64_t value)
    : type_(UserField::Integer)
    , i_value_(value) {
    }

    inline priv(const ::std::string& value)
    : type_(UserField::String)
    , i_value_(0)
    , s_value_(value) {
    }

    inline priv(const ::csdb::Amount& value)
    : type_(UserField::Amount)
    , i_value_(0)
    , a_value_(value) {
    }

    inline void put(::csdb::priv::obstream& os) const;
    inline void put_for_sig(::csdb::priv::obstream& os) const;
    inline bool get(::csdb::priv::ibstream& is);
    inline bool is_equal(const priv* other) const;

    DEFAULT_PRIV_CLONE()

    UserField::Type type_;
    uint64_t i_value_;
    ::std::string s_value_;
    ::csdb::Amount a_value_;
    friend class UserField;
};
SHARED_DATA_CLASS_IMPLEMENTATION_INLINE(UserField)

inline bool UserField::operator !=(const UserField& other) const noexcept
{
  return !operator ==(other);
}

template<>
UserField::UserField(uint64_t value);

template<>
UserField::UserField(const std::string& value);

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
