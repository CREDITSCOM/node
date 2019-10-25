#define TESTING

#include <algorithm>
#include <array>
#include <cstring>
#include <list>
#include <mutex>
#include <thread>

#include <csnode/node.hpp>
#include "clientconfigmock.hpp"
#include "gtest/gtest.h"
#include "solvermock.hpp"
#include "transportmock.hpp"

TEST(NodeTest, connection) {
    MockConfig config;

    ASSERT_EQ(true, true);
}
