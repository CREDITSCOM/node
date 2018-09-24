#define TESTING
#include <algorithm>
#include <array>
#include <cstring>
#include <list>
#include <mutex>
#include <thread>

#include "gtest/gtest.h"
#include <csnode/node.hpp>
#include "config.hpp"

class NodeTest : public ::testing::Test
{
  using NodePtr = std::shared_ptr<Node>;
protected:

  void SetUp()
  {
    po::variables_map vm;

    auto config = Config::read(vm);
    _globalNode = std::make_shared<Node>(config);
  }

  void TearDown() {}

  NodePtr _globalNode = nullptr;
};

TEST_F(NodeTest, connection)
{
  ASSERT_EQ(_globalNode->isGood(), true);
}
