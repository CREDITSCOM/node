#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/utils.hpp>

#include <csnode/conveyer.hpp>

#include <net/transport.hpp>

cs::PoolSynchronizer::PoolSynchronizer(const PoolSyncData&, Transport*, BlockChain*) {}

void cs::PoolSynchronizer::sync(cs::RoundNumber, cs::RoundNumber, bool) {}

void cs::PoolSynchronizer::syncLastPool() {}

void cs::PoolSynchronizer::getBlockReply(cs::PoolsBlock&&, std::size_t) {}

void cs::PoolSynchronizer::sendBlockRequest() {}

bool cs::PoolSynchronizer::isSyncroStarted() const { return false; }

bool cs::PoolSynchronizer::isOneBlockReply() const { return false; }

bool cs::PoolSynchronizer::isFastMode() const { return false; }

void cs::PoolSynchronizer::onConfigChanged(const Config&) {}
