#define TESTING

#include <gtest/gtest.h>
#include "CyclicBuffer.hpp"

TEST(CyclicBuffer, push_back)
{
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);

  ASSERT_EQ(1, buffer.back());
}

TEST(CyclicBuffer, back)
{
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);

  ASSERT_EQ(1, buffer.back());
}

TEST(CyclicBuffer, pop_back)
{
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);
  buffer.pop_back();

  ASSERT_EQ(0, buffer.size());
}

TEST(CyclicBuffer, push_front)
{
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1);

  ASSERT_EQ(1, buffer.front());
}

TEST(CyclicBuffer, pop_front)
{
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1);
  buffer.pop_front();

  ASSERT_EQ(0, buffer.size());
}

TEST(CyclicBuffer, get_element_by_index)
{
  CyclicBuffer<int, 10> buffer;
  for(int i = 1; i < 6; i++){
    buffer.push_back(i);
  }

  ASSERT_EQ(1, buffer[0]);
}

TEST(CyclicBuffer, full)
{
  CyclicBuffer<int, 10> buffer;
  for(int i = 1; i < 11; i++){
    buffer.push_back(i);
  }

  ASSERT_TRUE(buffer.full());
}

TEST(CyclicBuffer, empty)
{
  CyclicBuffer<int, 10> buffer;

  ASSERT_TRUE(buffer.empty());
}

TEST(CyclicBuffer, size)
{
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);

  ASSERT_EQ(1, buffer.size());
}