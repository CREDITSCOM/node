#define TESTING

#include <gtest/gtest.h>
#include <algorithm>
#include "dynamicbuffer.hpp"

using namespace cs;

TEST(DynamicBuffer, equal_by_size)
{
  DynamicBuffer a;
  DynamicBuffer b(a);

  ASSERT_TRUE(a == b);
}

TEST(DynamicBuffer, get_element_by_index)
{
  char str[] = "teststring";
  size_t size = strlen(str) + 1;
  DynamicBuffer a(size);
  auto buffer = a.get();
  strcpy(buffer, str);

  ASSERT_TRUE(a[3] == str[3]);
}

TEST(DynamicBuffer, stl_interface)
{
  char str[] = "cba";
  size_t size = strlen(str) + 1;
  DynamicBuffer a(size);
  auto buffer = a.get();
  strcpy(buffer, str);

  std::sort(a.begin(), a.end() - 1);

  ASSERT_STREQ("abc", *a);
}
