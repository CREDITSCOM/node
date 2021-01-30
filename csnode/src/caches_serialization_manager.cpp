#include <csnode/caches_serialization_manager.hpp>

#include <csnode/blockchain_serializer.hpp>
#include <csnode/smartcontracts_serializer.hpp>
#include <csnode/tokens_serializer.hpp>
#include <csnode/walletscache_serializer.hpp>
#include <csnode/walletsids_serializer.hpp>

namespace cs {

struct CachesSerializationManager::Impl {
    BlockChain_Serializer     blockchainSerializer;
    SmartContracts_Serializer smartContractsSerializer;
    TokensMaster_Serializer   tokensMasterSerializer;
    WalletsCache_Serializer   walletsCacheSerializer;
    WalletsIds_Serializer     walletsIdsSerializer;
};

CachesSerializationManager::CachesSerializationManager()
    : pImpl_(std::make_unique<Impl>()) {}

CachesSerializationManager::~CachesSerializationManager() = default;

void CachesSerializationManager::bind(
    BlockChain& blockchain,
    SmartContracts& smartcontracts,
    TokensMaster& tokensmaster,
    WalletsCache& walletscache,
    WalletsIds& walletsids
) {
    pImpl_->blockchainSerializer.bind(blockchain);
    pImpl_->smartContractsSerializer.bind(smartcontracts);
    pImpl_->tokensMasterSerializer.bind(tokensmaster);
    pImpl_->walletsCacheSerializer.bind(walletscache);
    pImpl_->walletsIdsSerializer.bind(walletsids);
}

void CachesSerializationManager::save() {
  pImpl_->blockchainSerializer.save();
  pImpl_->smartContractsSerializer.save();
  pImpl_->tokensMasterSerializer.save();
  pImpl_->walletsCacheSerializer.save();
  pImpl_->walletsIdsSerializer.save();
}

void CachesSerializationManager::load() {
  pImpl_->blockchainSerializer.load();
  pImpl_->smartContractsSerializer.load();
  pImpl_->tokensMasterSerializer.load();
  pImpl_->walletsCacheSerializer.load();
  pImpl_->walletsIdsSerializer.load();
}

} // namespace cs
