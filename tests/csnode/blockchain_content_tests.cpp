#define TESTING

#include "clientconfigmock.hpp"
#include "gtest/gtest.h"

#include <csnode/blockchain.hpp>

#include <csdb/pool.hpp>
#include <lib/system/utils.hpp>

// an "uncertain" own block is replaced in place by the network's version only when testContentEqual() holds;
// rewards and fees are credited through the trusted mask, so a different mask must not count as equal content
TEST(BlockChainContent, ContentEqualRequiresSameTrustedMask) {
    csdb::Pool own;
    own.set_sequence(10);
    own.add_number_trusted(3);
    own.add_real_trusted(0);

    csdb::Pool canonical = own.clone();
    ASSERT_TRUE(BlockChain::testContentEqual(own, canonical));

    canonical.add_real_trusted(cs::Utils::maskToBits(cs::Bytes{ 0, 255, 1 }));
    ASSERT_FALSE(BlockChain::testContentEqual(own, canonical));
}
