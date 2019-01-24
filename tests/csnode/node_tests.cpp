#define TESTING

#include <algorithm>
#include <array>
#include <cstring>
#include <list>
#include <mutex>
#include <thread>

#include "gtest/gtest.h"
#include <csnode/node.hpp>
#include "clientconfigmock.hpp"
#include "nodemock.hpp"
#include "solvermock.hpp"
#include "transportmock.hpp"
#include "networkmock.hpp"

TEST(NodeTest, connection)
{
  MockConfig config;

  ASSERT_EQ(true, true);
}
