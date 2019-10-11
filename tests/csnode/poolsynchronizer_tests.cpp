#include <gtest/gtest.h>

#include <observer.hpp>

#include <net/transport.hpp>

#include <csnode/node.hpp>
#include <csnode/blockchain.hpp>
#include <csnode/poolsynchronizer.hpp>

#include <boost/program_options/variables_map.hpp>

const csdb::Address genesisAddress = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address startAddress = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

TEST(PoolSynchronizer, TestSyncLastPool) {
    Config config;
    boost::program_options::variables_map map;

    cs::config::Observer observer(config, map);

    Node node(config, observer);
    Transport transport(config, &node);
    BlockChain blockChain(genesisAddress, startAddress);

    cs::PoolSynchronizer synchronizer(config.getPoolSyncSettings(), &transport, &blockChain);
}
