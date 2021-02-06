#include <csnode/caches_serialization_manager.hpp>

#include <exception>

#include <csnode/blockchain_serializer.hpp>
#include <csnode/smartcontracts_serializer.hpp>
#include <csnode/tokens_serializer.hpp>
#include <csnode/walletscache_serializer.hpp>
#include <csnode/walletsids_serializer.hpp>

#include <lib/system/logger.hpp>

namespace cs {

struct CachesSerializationManager::Impl {
    BlockChain_Serializer     blockchainSerializer;
    SmartContracts_Serializer smartContractsSerializer;
#ifdef NODE_API
    TokensMaster_Serializer   tokensMasterSerializer;
#endif
    WalletsCache_Serializer   walletsCacheSerializer;
    WalletsIds_Serializer     walletsIdsSerializer;

    enum BindBits {
      BlockChainBit,
      SmartContractsBit,
      WalletsCacheBit,
      WalletsIdsBit
#ifdef NODE_API
      ,TokensMasterBit
#endif
    };

    uint8_t bindFlags = 0;

    bool bindingsReady() {
        return (
            (bindFlags & (1 << BlockChainBit)) &&
            (bindFlags & (1 << SmartContractsBit)) &&
            (bindFlags & (1 << WalletsCacheBit)) &&
            (bindFlags & (1 << WalletsIdsBit))
#ifdef NODE_API
            && (bindFlags & (1 << TokensMasterBit))
#endif
        );
    }

    void clear() {
        blockchainSerializer.clear();
        smartContractsSerializer.clear();
#ifdef NODE_API
        tokensMasterSerializer.clear();
#endif
        walletsCacheSerializer.clear();
        walletsIdsSerializer.clear();
    }
};

CachesSerializationManager::CachesSerializationManager()
    : pImpl_(std::make_unique<Impl>()) {}

CachesSerializationManager::~CachesSerializationManager() = default;

void CachesSerializationManager::bind(BlockChain& bc) {
    pImpl_->blockchainSerializer.bind(bc);
    pImpl_->bindFlags |= (1 << Impl::BlockChainBit);
}

void CachesSerializationManager::bind(SmartContracts& sc) {
    pImpl_->smartContractsSerializer.bind(sc);
    pImpl_->bindFlags |= (1 << Impl::SmartContractsBit);
}

void CachesSerializationManager::bind(WalletsCache& wc) {
    pImpl_->walletsCacheSerializer.bind(wc);
    pImpl_->bindFlags |= (1 << Impl::WalletsCacheBit);
}

void CachesSerializationManager::bind(WalletsIds& wi) {
    pImpl_->walletsIdsSerializer.bind(wi);
    pImpl_->bindFlags |= (1 << Impl::WalletsIdsBit);
}

void CachesSerializationManager::bind([[maybe_unused]] TokensMaster& tm) {
#ifdef NODE_API
    pImpl_->tokensMasterSerializer.bind(tm);
    pImpl_->bindFlags |= (1 << Impl::TokensMasterBit);
#endif
}

bool CachesSerializationManager::save() {
    if (!pImpl_->bindingsReady()) {
        cserror() << "CachesSerializationManager: save error: "
                  << "bindings are not ready";
        return false;
    }

    try {
        pImpl_->blockchainSerializer.save();
        pImpl_->smartContractsSerializer.save();
        pImpl_->walletsCacheSerializer.save();
        pImpl_->walletsIdsSerializer.save();
#ifdef NODE_API
        pImpl_->tokensMasterSerializer.save();
#endif
    } catch (const std::exception& e) {
        cserror() << "CachesSerializationManager: error on save: "
                  << e.what();
        return false;
    } catch (...) {
        cserror() << "CachesSerializationManager: unknown save error ";
        return false;
    }
    return true;
}

bool CachesSerializationManager::load() {
    if (!pImpl_->bindingsReady()) {
        cserror() << "CachesSerializationManager: load error: "
                  << "bindings are not ready";
        return false;
    }

    try {
        pImpl_->blockchainSerializer.load();
        pImpl_->smartContractsSerializer.load();
        pImpl_->walletsCacheSerializer.load();
        pImpl_->walletsIdsSerializer.load();
#ifdef NODE_API
        pImpl_->tokensMasterSerializer.load();
#endif
    } catch (const std::exception& e) {
        cserror() << "CachesSerializationManager: error on load: "
                  << e.what();
        pImpl_->clear();
        return false;
    } catch (...) {
        cserror() << "CachesSerializationManager: unknown error on load";
        pImpl_->clear();
        return false;
    }
    return true;
}

} // namespace cs
