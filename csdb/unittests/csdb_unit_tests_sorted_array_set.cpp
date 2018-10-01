#include "csdb/internal/sorted_array_set.h"

#include <gtest/gtest.h>

using namespace ::csdb::internal;

TEST(SortedArraySet, DISABLED_Basic_Defaults)
{
  sorted_array_set set(nullptr, 0, 0);

  EXPECT_EQ(set.size(), 0);
}

TEST(SortedArraySet, Basic_Size)
{
  {
    uint8_t array[] = { 1, 2, 3, 4 };

    sorted_array_set set(array, 4, 1);

    EXPECT_EQ(set.size(), 4);
  }

  {
    uint8_t array[] = { 1, 2, 3, 4 };

    sorted_array_set set(array, 2, 2);

    EXPECT_EQ(set.size(), 2);
  }
}

TEST(SortedArraySet, Basic_Contains)
{
  uint8_t array[] = { 1, 2, 3, 4 };

  sorted_array_set set(array, 4, 1);

  {
    uint8_t item = 1;
    EXPECT_TRUE(set.contains(&item));
  }

  {
    uint8_t item = 4;
    EXPECT_TRUE(set.contains(&item));
  }

  {
    uint8_t item = 5;
    EXPECT_FALSE(set.contains(&item));
  }
}

TEST(SortedArraySet, Basic_GetIndex)
{
  uint8_t array[] = { 1, 2, 3, 4 };

  {
    sorted_array_set set(array, 4, 1);

    {
      uint8_t item = 1;
      EXPECT_EQ(*set[set.getIndex(&item)], 1);
    }

    {
      uint8_t item = 4;
      EXPECT_EQ(*set[set.getIndex(&item)], 4);
    }

    {
      uint8_t item = 5;
      EXPECT_EQ(set.getIndex(&item), set.size());
    }
  }

  {
    sorted_array_set set(array, 2, 2);

    {
      uint8_t item[] = {1 , 2};
      size_t index = set.getIndex(item);
      EXPECT_EQ(index, 0);
    }

    {
      uint8_t item[] = {3 , 4};
      size_t index = set.getIndex(item);
      EXPECT_EQ(index, 1);
    }

    {
      uint8_t item[] = {5, 6};
      size_t index = set.getIndex(item);
      EXPECT_EQ(index, set.size());
    }
  }
}

//
// Complex tests
//

#include <algorithm>
#include <vector>

namespace
{
  static constexpr size_t NUM_ITEMS = 10;

  template <int N>
  struct Item
  {
    static constexpr size_t NUM_BYTES = N;
    uint8_t data[N];

    bool operator<(const Item& o) const
    {
      auto res = std::lexicographical_compare(data, data + NUM_BYTES, o.data, o.data + NUM_BYTES);
      return res;
    }

    bool operator==(const Item& o) const
    {
      return memcmp(data, o.data, NUM_BYTES) == 0;
    }

    static Item generateItem()
    {
      Item item;

      std::generate(item.data, item.data + NUM_BYTES, []()
      {
        return rand();
      });

      return item;
    }
  };

  template <class T, class A = std::vector<T>>
  A generateArray(size_t numItems)
  {
    A array;
    array.resize(numItems);

    std::generate(std::begin(array), std::end(array), []()
      {
        return T::generateItem();
      });

    std::sort(std::begin(array), std::end(array));

    return array;
  }
}

constexpr size_t NUM_ELEMENTS = 200 * 1000;

using Item32 = Item<32>;

TEST(SortedArraySet, Complex_Contains)
{
  using Item = Item32;
  auto source = generateArray<Item>(NUM_ELEMENTS);

  assert(std::is_sorted(std::begin(source), std::end(source)));

  sorted_array_set set(source.data(), source.size(), sizeof(Item));

  for (size_t i = 0; i < 20; ++i)
  {
    auto index = (rand() * rand()) % source.size();
    const Item& item = source[index];

    auto found = set.contains(&item);
    EXPECT_TRUE(found);
  }
}

TEST(SortedArraySet, Complex_GetIndex)
{
  using Item = Item32;
  auto source = generateArray<Item>(NUM_ELEMENTS);

  assert(std::is_sorted(std::begin(source), std::end(source)));

  sorted_array_set set(source.data(), source.size(), sizeof(Item));

  for (size_t i = 0; i < 20; ++i)
  {
    auto index = (rand() * rand()) % source.size();
    const Item& item = source[index];

    auto foundIndex = set.getIndex(&item);
    EXPECT_TRUE(index == foundIndex);
  }
}

TEST(SortedArraySet, Complex_SquareBraces)
{
  using Item = Item32;
  auto source = generateArray<Item>(NUM_ELEMENTS);

  assert(std::is_sorted(std::begin(source), std::end(source)));

  sorted_array_set set(source.data(), source.size(), sizeof(Item));

  for (size_t i = 0; i < 20; ++i)
  {
    auto index = (rand() * rand()) % source.size();
    const Item& item = source[index];

    auto foundIndex = set.getIndex(&item);

    auto equal = (memcmp( &item, set[foundIndex], Item::NUM_BYTES ) == 0);
    EXPECT_TRUE(equal);
  }
}

TEST(SortedArraySet, Complex_InitFromVoid)
{
  using Item = Item32;
  auto source = generateArray<Item32>(NUM_ELEMENTS);

  assert(std::is_sorted(std::begin(source), std::end(source)));

  const void* ptr = static_cast<const void*>(source.data());
  sorted_array_set set(ptr, source.size(), sizeof(Item));

  for (size_t i = 0; i < 20; ++i)
  {
    auto index = (rand() * rand()) % source.size();
    const Item& item = source[index];

    auto found = set.contains(&item);
    EXPECT_TRUE(found);
  }
}

TEST(SortedArraySet, Complex_ItemLength13)
{
  using Item = Item<13>;
  auto source = generateArray<Item>(NUM_ELEMENTS);

  assert(std::is_sorted(std::begin(source), std::end(source)));

  const void* ptr = static_cast<const void*>(source.data());
  sorted_array_set set(ptr, source.size(), sizeof(Item));

  for (size_t i = 0; i < 20; ++i)
  {
    auto index = (rand() * rand()) % source.size();
    const Item& item = source[index];

    auto found = set.contains(&item);
    EXPECT_TRUE(found);
  }
}

//
// Helper functions
//

TEST(SortedArraySet, Helpers_Sort)
{
  constexpr size_t BYTES_PER_ELEMENT = 16;
  using Item = Item<BYTES_PER_ELEMENT>;
  using Array = std::vector<Item>;

  Array array(NUM_ELEMENTS);

  std::generate(std::begin(array), std::end(array), []()
    {
      return Item::generateItem();
    });

  sorted_array_set::sort<BYTES_PER_ELEMENT>(array.data(), array.size());

  auto sorted = (std::is_sorted(std::begin(array), std::end(array)));
  EXPECT_TRUE(sorted);
}

TEST(SortedArraySet, Helpers_IsSorted)
{
  constexpr size_t BYTES_PER_ELEMENT = 16;
  using Item = Item<BYTES_PER_ELEMENT>;
  using Array = std::vector<Item>;

  Array array(NUM_ELEMENTS);

  std::generate(std::begin(array), std::end(array), []()
  {
    return Item::generateItem();
  });

  sorted_array_set::sort<BYTES_PER_ELEMENT>(array.data(), array.size());

  auto sorted = sorted_array_set::isSorted<BYTES_PER_ELEMENT>(array.data(), array.size());
  EXPECT_TRUE(sorted);
}

/*
TEST(CopyAssign, DISABLED_ShouldNotCompile)
{
  sorted_array_set a;
  sorted_array_set b(a);
  sorted_array_set c = a;
}
*/

//
// Tests for templated version
//

TEST(SortedArraySetTemplate, Complex_Contains)
{
  using Item = Item32;
  auto source = generateArray<Item32>(NUM_ELEMENTS);

  assert(std::is_sorted(std::begin(source), std::end(source)));

  const void* ptr = static_cast<const void*>(source.data());

  sorted_array_set_t<Item::NUM_BYTES> set(ptr, source.size());

  for (size_t i = 0; i < 1000; ++i)
  {
    auto index = (rand() * rand()) % source.size();
    const Item& item = source[index];

    auto found = set.contains(&item);
    EXPECT_TRUE(found);
  }
}

TEST(SortedArraySetTemplate, Complex_GetIndex)
{
  using Item = Item32;
  auto source = generateArray<Item>(NUM_ELEMENTS);

  assert(std::is_sorted(std::begin(source), std::end(source)));

  sorted_array_set_t<Item::NUM_BYTES> set(source.data(), source.size());

  for (size_t i = 0; i < 20; ++i)
  {
    auto index = (rand() * rand()) % source.size();
    const Item& item = source[index];

    auto foundIndex = set.getIndex(&item);
    EXPECT_TRUE(index == foundIndex);
  }
}

TEST(SortedArraySetTemplate, Complex_SquareBraces)
{
  using Item = Item32;
  auto source = generateArray<Item>(NUM_ELEMENTS);

  assert(std::is_sorted(std::begin(source), std::end(source)));

  sorted_array_set_t<Item::NUM_BYTES> set(source.data(), source.size());

  for (size_t i = 0; i < 20; ++i)
  {
    auto index = (rand() * rand()) % source.size();
    const Item& item = source[index];

    auto foundIndex = set.getIndex(&item);

    auto equal = (memcmp(&item, set[foundIndex], Item::NUM_BYTES) == 0);
    EXPECT_TRUE(equal);
  }
}

TEST(SortedArraySetTemplate, Helpers_Sort)
{
  using Item = Item32;
  auto source = generateArray<Item32>(NUM_ELEMENTS);

  sorted_array_set_t<32>::sort(source.data(), source.size());

  auto sorted = (std::is_sorted(std::begin(source), std::end(source)));
  EXPECT_TRUE(sorted);
}

TEST(SortedArraySetTemplate, Helpers_IsSorted)
{
  using Item = Item32;
  auto source = generateArray<Item32>(NUM_ELEMENTS);

  sorted_array_set_t<32>::sort(source.data(), source.size());

  auto sorted = sorted_array_set_t<32>::isSorted(source.data(), source.size());
  EXPECT_TRUE(sorted);
}
