#include <csnode/caches_serialization_manager.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

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

    const std::string kHashesFile = "quick_start_hashes.dat";
    const std::string kQuickStartRoot = "qs";

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
        blockchainSerializer.clear(kQuickStartRoot);
        smartContractsSerializer.clear(kQuickStartRoot);
#ifdef NODE_API
        tokensMasterSerializer.clear(kQuickStartRoot);
#endif
        walletsCacheSerializer.clear(kQuickStartRoot);
        walletsIdsSerializer.clear(kQuickStartRoot);
    }

    template <class T>
    void addHash(std::vector<uint8_t>& result, T& entity) {
      auto hash = entity.hash();
      result.insert(result.end(), hash.begin(), hash.end());
    }

    std::string getHashes() {
      std::vector<uint8_t> result;

      addHash(result, blockchainSerializer);
      addHash(result, smartContractsSerializer);
      addHash(result, walletsCacheSerializer);
      addHash(result, walletsIdsSerializer);
#ifdef NODE_API
      addHash(result, tokensMasterSerializer);
#endif

      return cscrypto::helpers::bin2Hex(
        result.data(),
        result.size()
      );
    }

    void saveHashes() {
        std::ofstream f(kHashesFile);
        f << getHashes();
    }

    bool checkHashes() {
        csinfo() << "Start check hashes...";
        auto currentHashes = getHashes();
        std::ifstream f(kHashesFile);
        std::string writtenHashes;
        f >> writtenHashes;
        csinfo() << "current hashes is:\n"
                 << currentHashes
                 << ", written hases is:\n"
                 << writtenHashes;
        return currentHashes == writtenHashes;
    }
};

CachesSerializationManager::CachesSerializationManager()
    : pImpl_(std::make_unique<Impl>()) {
  if (!std::filesystem::exists(pImpl_->kQuickStartRoot)
      || !std::filesystem::is_directory(pImpl_->kQuickStartRoot)) {
    std::filesystem::create_directories(pImpl_->kQuickStartRoot);
  }
}

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
        pImpl_->blockchainSerializer.save(pImpl_->kQuickStartRoot);
        pImpl_->smartContractsSerializer.save(pImpl_->kQuickStartRoot);
        pImpl_->walletsCacheSerializer.save(pImpl_->kQuickStartRoot);
        pImpl_->walletsIdsSerializer.save(pImpl_->kQuickStartRoot);
#ifdef NODE_API
        pImpl_->tokensMasterSerializer.save(pImpl_->kQuickStartRoot);
#endif
        pImpl_->saveHashes();
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
        pImpl_->blockchainSerializer.load(pImpl_->kQuickStartRoot);
        pImpl_->smartContractsSerializer.load(pImpl_->kQuickStartRoot);
        pImpl_->walletsCacheSerializer.load(pImpl_->kQuickStartRoot);
        pImpl_->walletsIdsSerializer.load(pImpl_->kQuickStartRoot);
#ifdef NODE_API
        pImpl_->tokensMasterSerializer.load(pImpl_->kQuickStartRoot);
#endif
        if (!pImpl_->checkHashes()) {
            cserror() << "CachesSerializationManager: invalid hashes on load";
            pImpl_->clear();
            return false;
        }
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

void CachesSerializationManager::clear() {
    if (!pImpl_->bindingsReady()) {
        return;
    }
    pImpl_->clear();
}

} // namespace cs
