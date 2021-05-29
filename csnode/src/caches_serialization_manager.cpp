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

    void clear(size_t version) {
        std::filesystem::path p(kQuickStartRoot);
        p /= std::to_string(version);

        try {
            blockchainSerializer.clear(p);
            smartContractsSerializer.clear(p);
#ifdef NODE_API
            tokensMasterSerializer.clear(p);
#endif
            walletsCacheSerializer.clear(p);
            walletsIdsSerializer.clear(p);
            std::filesystem::remove_all(p);
        }
        catch (const std::exception& e) {
            cswarning() << "CachesSerializationManager: error on clear: " << e.what();
        }
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

    void saveHashes(size_t version) {
        std::ofstream f(
          std::filesystem::path(kQuickStartRoot) /
          std::to_string(version) /
          kHashesFile
        );
        f << getHashes();
    }

    bool checkHashes(size_t version) {
        csinfo() << "Start check hashes...";
        auto currentHashes = getHashes();
        std::ifstream f(
          std::filesystem::path(kQuickStartRoot) /
          std::to_string(version) /
          kHashesFile
        );
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

bool CachesSerializationManager::save(size_t version) {
    if (!pImpl_->bindingsReady()) {
        cserror() << "CachesSerializationManager: save error: "
                  << "bindings are not ready";
        return false;
    }

    try {
        std::filesystem::path p(pImpl_->kQuickStartRoot);
        p /= std::to_string(version);
        if (!std::filesystem::exists(p) || !std::filesystem::is_directory(p)) {
          std::filesystem::create_directories(p);
        }

        pImpl_->blockchainSerializer.save(p);
        pImpl_->smartContractsSerializer.save(p);
        pImpl_->walletsCacheSerializer.save(p);
        pImpl_->walletsIdsSerializer.save(p);
#ifdef NODE_API
        pImpl_->tokensMasterSerializer.save(p);
#endif
        pImpl_->saveHashes(version);
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

bool CachesSerializationManager::load(size_t version) {
    if (!pImpl_->bindingsReady()) {
        cserror() << "CachesSerializationManager: load error: "
                  << "bindings are not ready";
        return false;
    }

    try {
        std::filesystem::path p(pImpl_->kQuickStartRoot);
        p /= std::to_string(version);

        pImpl_->blockchainSerializer.load(p);
        pImpl_->smartContractsSerializer.load(p);
        pImpl_->walletsCacheSerializer.load(p);
        pImpl_->walletsIdsSerializer.load(p);
#ifdef NODE_API
        pImpl_->tokensMasterSerializer.load(p);
#endif
        if (!pImpl_->checkHashes(version)) {
            cserror() << "CachesSerializationManager: invalid hashes on load";
            pImpl_->clear(version);
            return false;
        }
    } catch (const std::exception& e) {
        cserror() << "CachesSerializationManager: error on load: "
                  << e.what();
        pImpl_->clear(version);
        return false;
    } catch (...) {
        cserror() << "CachesSerializationManager: unknown error on load";
        pImpl_->clear(version);
        return false;
    }
    return true;
}

void CachesSerializationManager::clear(size_t version) {
    if (!pImpl_->bindingsReady()) {
        return;
    }
    pImpl_->clear(version);
}

} // namespace cs
