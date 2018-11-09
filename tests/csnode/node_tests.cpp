#define TESTING

#include <algorithm>
#include <array>
#include <cstring>
#include <list>
#include <mutex>
#include <thread>

#include "gtest/gtest.h"
#include <csnode/node.hpp>
#include "ClientConfigMock.hpp"
#include "NodeMock.hpp"
#include "SolverMock.hpp"
#include "TransportMock.hpp"
#include "NetworkMock.hpp"

TEST(NodeTest, connection)
{
  MockConfig config;
  TransportMock transportMock();

  ASSERT_EQ(true, true);
}
