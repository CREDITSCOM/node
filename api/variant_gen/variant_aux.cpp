#include <general_types.h>
#include <algorithm>

namespace general {

bool operator<(const _Variant__isset& a, const _Variant__isset& b) {
  auto ta = std::tie(a.v_bool, a.v_double, a.v_i16, a.v_i32, a.v_i64, a.v_i8, a.v_list, a.v_map, a.v_set, a.v_string);
  decltype(ta) tb =
      std::tie(b.v_bool, b.v_double, b.v_i16, b.v_i32, b.v_i64, b.v_i8, b.v_list, b.v_map, b.v_set, b.v_string);
  return ta < tb;
}

bool Variant::operator<(const Variant& that) const {
  const Variant &a = *this, &b = that;
  if (a.__isset < b.__isset) {
    return true;
  }
  if (b.__isset < a.__isset) {
    return false;
  }
  if (a.__isset.v_bool) {
    return a.v_bool < b.v_bool;
  }
  if (a.__isset.v_double) {
    return a.v_double < b.v_double;
  }
  if (a.__isset.v_i16) {
    return a.v_i16 < b.v_i16;
  }
  if (a.__isset.v_i32) {
    return a.v_i32 < b.v_i32;
  }
  if (a.__isset.v_i64) {
    return a.v_i64 < b.v_i64;
  }
  if (a.__isset.v_i8) {
    return a.v_i8 < b.v_i8;
  }
  if (a.__isset.v_string) {
    return a.v_string < b.v_string;
  }
  if (a.__isset.v_list) {
    return std::lexicographical_compare(a.v_list.begin(), a.v_list.end(), b.v_list.begin(), b.v_list.end());
  }
  if (a.__isset.v_set) {
    return std::lexicographical_compare(a.v_set.begin(), a.v_set.end(), b.v_set.begin(), b.v_set.end());
  }
  if (a.__isset.v_map) {
    return std::lexicographical_compare(a.v_map.begin(), a.v_map.end(), b.v_map.begin(), b.v_map.end());
  }
  assert(false);
  return false;
}
}
