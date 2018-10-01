#include "csdb_unit_tests_shared_data.h"

#include <vector>

#include <gtest/gtest.h>

class SharedDataTest : public ::testing::Test
{
protected:
  void TearDown()
  {
    EXPECT_EQ(TestSharedData::counter(), 0);
  }
};

TEST_F(SharedDataTest, CreateAndDestroy)
{
  TestSharedData a;
  EXPECT_EQ(a.value(), 0);
  EXPECT_EQ(a.is_copied(), false);
}

TEST_F(SharedDataTest, Copy)
{
  TestSharedData a;
  EXPECT_EQ(a.value(), 0);
  TestSharedData b(a);
  EXPECT_EQ(b.value(), 0);

  EXPECT_EQ(a.is_copied(), false);
  EXPECT_EQ(b.is_copied(), false);
  EXPECT_EQ(a.is_same(b), true);
}

TEST_F(SharedDataTest, Assignment)
{
  TestSharedData a;
  EXPECT_EQ(a.value(), 0);
  TestSharedData b;
  EXPECT_EQ(b.value(), 0);
  b = a;
  EXPECT_EQ(b.value(), 0);

  EXPECT_EQ(a.is_copied(), false);
  EXPECT_EQ(b.is_copied(), false);
  EXPECT_EQ(a.is_same(b), true);
}

TEST_F(SharedDataTest, AssignmentAndCopyMixed)
{
  TestSharedData a;
  EXPECT_EQ(a.value(), 0);
  TestSharedData b;
  EXPECT_EQ(b.value(), 0);
  b = a;
  EXPECT_EQ(b.value(), 0);
  TestSharedData c(a);
  EXPECT_EQ(c.value(), 0);

  EXPECT_EQ(a.is_copied(), false);
  EXPECT_EQ(b.is_copied(), false);
  EXPECT_EQ(c.is_copied(), false);
  EXPECT_EQ(a.is_same(b), true);
  EXPECT_EQ(a.is_same(c), true);
}

TEST_F(SharedDataTest, NoCopyChange)
{
  TestSharedData a;
  EXPECT_EQ(a.value(), 0);
  a.setValue(1);
  EXPECT_EQ(a.value(), 1);
  a.setValue(2);
  EXPECT_EQ(a.value(), 2);

  EXPECT_EQ(a.is_copied(), false);
}

TEST_F(SharedDataTest, SimpleCopyOnWrite)
{
  TestSharedData a;
  EXPECT_EQ(a.value(), 0);
  TestSharedData b(a);
  EXPECT_EQ(b.value(), 0);
  EXPECT_EQ(a.is_copied(), false);
  EXPECT_EQ(b.is_copied(), false);
  EXPECT_EQ(a.is_same(b), true);

  b.setValue(1);
  EXPECT_EQ(b.value(), 1);
  EXPECT_EQ(a.is_copied(), false);
  EXPECT_EQ(b.is_copied(), true);
}

TEST_F(SharedDataTest, TripleCopyOnWrite)
{
  TestSharedData a;
  EXPECT_EQ(a.value(), 0);
  TestSharedData b(a);
  EXPECT_TRUE(b.copy_semantic_used());
  EXPECT_EQ(b.value(), 0);
  EXPECT_EQ(a.is_copied(), false);
  EXPECT_EQ(b.is_copied(), false);
  EXPECT_EQ(a.is_same(b), true);

  b.setValue(1);
  EXPECT_EQ(b.value(), 1);
  EXPECT_EQ(a.is_copied(), false);
  EXPECT_EQ(b.is_copied(), true);

  TestSharedData c;
  c = a;
  EXPECT_EQ(c.value(), 0);
  EXPECT_EQ(c.is_copied(), false);
  EXPECT_EQ(c.is_same(a), true);
  EXPECT_EQ(c.is_same(b),false);

  c.setValue(2);
  EXPECT_EQ(c.value(), 2);
  EXPECT_EQ(c.is_copied(), true);
}

namespace {
  TestSharedData get_test_data(int value)
  {
    return TestSharedData(value);
  }
} // namespace

TEST_F(SharedDataTest, MoveConstructorAndAssignment)
{
  std::vector<TestSharedData> a(1);
  EXPECT_FALSE(a[0].move_semantic_used());
  EXPECT_FALSE(a[0].copy_semantic_used());
  a[0].setValue(2);
  a.insert(a.begin(), TestSharedData(1));
  EXPECT_EQ(a[0].value(), 1);
  EXPECT_EQ(a[1].value(), 2);
  EXPECT_TRUE(a[0].move_semantic_used());
  EXPECT_TRUE(a[1].move_semantic_used());
  EXPECT_FALSE(a[0].copy_semantic_used());
  EXPECT_FALSE(a[1].copy_semantic_used());

  a[1] = get_test_data(100);
  EXPECT_EQ(a[1].value(), 100);
  EXPECT_TRUE(a[1].move_semantic_used());
  EXPECT_FALSE(a[1].copy_semantic_used());
}

namespace {
  void tsd_assign(TestSharedData& a, const TestSharedData* b)
  {
    a = *b;
  }
} // namespace

TEST_F(SharedDataTest, SelfAssignment)
{
  TestSharedData a(100);
  EXPECT_EQ(a.value(), 100);
  EXPECT_FALSE(a.is_copied());
  EXPECT_FALSE(a.move_semantic_used());
  EXPECT_FALSE(a.copy_semantic_used());

  tsd_assign(a, &a);
  EXPECT_EQ(a.value(), 100);
  EXPECT_FALSE(a.is_copied());
  EXPECT_FALSE(a.move_semantic_used());
  EXPECT_FALSE(a.copy_semantic_used());

  TestSharedData b(200);
  tsd_assign(a, &b);
  EXPECT_EQ(a.value(), 200);
  EXPECT_TRUE(a.is_same(b));
  EXPECT_FALSE(a.is_copied());
  EXPECT_FALSE(a.move_semantic_used());
  EXPECT_TRUE(a.copy_semantic_used());
}
