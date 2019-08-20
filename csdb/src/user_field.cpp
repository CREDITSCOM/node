#include <csdb/user_field.hpp>

#include "binary_streams.hpp"

namespace csdb {

inline void UserField::priv::put(::csdb::priv::obstream& os) const {
    switch (type_) {
        case UserField::Integer:
            os.put(type_);
            os.put(i_value_);
            break;

        case UserField::String:
            os.put(type_);
            os.put(s_value_);
            break;

        case UserField::Amount:
            os.put(type_);
            os.put(a_value_);
            break;

        default:
            break;
    }
}

inline void UserField::priv::put_for_sig(::csdb::priv::obstream& os) const {
    switch (type_) {
        case UserField::Integer:
            os.put(i_value_);
            break;

        case UserField::String:
            os.put(s_value_);
            break;

        case UserField::Amount:
            os.put(a_value_);
            break;

        default:
            break;
    }
}

inline bool UserField::priv::get(::csdb::priv::ibstream& is) {
    UserField::Type type;
    if (!is.get(type)) {
        return false;
    }
    switch (type) {
        case UserField::Integer:
            if (!is.get(i_value_)) {
                return false;
            }
            break;

        case UserField::String:
            if (!is.get(s_value_)) {
                return false;
            }
            break;

        case UserField::Amount:
            if (!is.get(a_value_)) {
                return false;
            }
            break;

        default:
            return false;
    }
    type_ = type;
    return true;
}

inline bool UserField::priv::is_equal(const priv* other) const {
    if (type_ != other->type_) {
        return false;
    }

    switch (type_) {
        case UserField::Integer:
            return (i_value_ == other->i_value_);
        case UserField::String:
            return (s_value_ == other->s_value_);
        case UserField::Amount:
            return (a_value_ == other->a_value_);
        default:
            return true;
    }
}

template <>
UserField::UserField(uint64_t value)
: d(new priv(value)) {
}

template <>
UserField::UserField(const ::std::string& value)
: d(new priv(value)) {
}

template <>
UserField::UserField(const ::csdb::Amount& value)
: d(new priv(value)) {
}

UserField::UserField(const char* value)
: d(new priv(::std::string(value))) {
}

bool UserField::is_valid() const noexcept {
    return (Unknown != d->type_);
}

UserField::Type UserField::type() const noexcept {
    return d->type_;
}

template <>
uint64_t UserField::value<uint64_t>() const noexcept {
    const priv* data = d.constData();
    return (Integer == data->type_) ? data->i_value_ : 0;
}

template <>
::std::string UserField::value<::std::string>() const noexcept {
    const priv* data = d.constData();
    return (String == data->type_) ? data->s_value_ : ::std::string{};
}

template <>
::csdb::Amount UserField::value<::csdb::Amount>() const noexcept {
    const priv* data = d.constData();
    return (Amount == data->type_) ? data->a_value_ : 0_c;
}

bool UserField::operator==(const UserField& other) const noexcept {
    return d->is_equal(other.d);
}

void UserField::put(::csdb::priv::obstream& os) const {
    d->put(os);
}

void UserField::put_for_sig(::csdb::priv::obstream& os) const {
    d->put_for_sig(os);
}

bool UserField::get(::csdb::priv::ibstream& is) {
    return d->get(is);
}

}  // namespace csdb
