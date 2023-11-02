#include <csnode/caches_serialization_manager.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

#include <csnode/blockchain_serializer.hpp>
#include <csnode/smartcontracts_serializer.hpp>
#include <csnode/tokens_serializer.hpp>
#include <csnode/walletscache_serializer.hpp>
#include <csnode/walletsids_serializer.hpp>
#include <csnode/apihandler_serializer.hpp>
#include <csconnector/csconnector.hpp>

#include <lib/system/logger.hpp>

namespace cs {

struct CachesSerializationManager::Impl {
    BlockChain_Serializer     blockchainSerializer;
    SmartContracts_Serializer smartContractsSerializer;
#ifdef NODE_API
    TokensMaster_Serializer   tokensMasterSerializer;
    APIHandler_Serializer     apiHandlerSerializer;
#endif
    WalletsCache_Serializer   walletsCacheSerializer;
    WalletsIds_Serializer     walletsIdsSerializer;

    const std::string kHashesFile = "quick_start_hashes.dat";
    const std::string kQuickStartRoot = "qs";
    const std::vector <std::string> hashesDivisions = {"blockchain", "smartcontracts","walletscache","walletsIds","tokensmaster","apihandler"};

    enum BindBits {
      BlockChainBit,
      SmartContractsBit,
      WalletsCacheBit,
      WalletsIdsBit
#ifdef NODE_API
      , TokensMasterBit
      , APIHandlerBit
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
            && (bindFlags & (1 << APIHandlerBit))
#endif
        );
    }

    void clear(size_t version) {
        csinfo() << "CachesSerializationManager: try to clear version " << version;
        std::filesystem::path p(kQuickStartRoot);
        p /= std::to_string(version);

        try {
            blockchainSerializer.clear(p);
            smartContractsSerializer.clear(p);
#ifdef NODE_API
            tokensMasterSerializer.clear(p);
            apiHandlerSerializer.clear(p);
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
      csdebug() << "Got blockchain hashes";
      addHash(result, smartContractsSerializer);
      csdebug() << "Got smartcontracts hashes";
      addHash(result, walletsCacheSerializer);
      csdebug() << "Got walletscache hashes";
      addHash(result, walletsIdsSerializer);
      csdebug() << "Got walletids hashes";
#ifdef NODE_API
      addHash(result, tokensMasterSerializer);
      csdebug() << "Got tokensmaster hashes";
      addHash(result, apiHandlerSerializer);
      csdebug() << "Got apihandler hashes";
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

    std::vector<std::string> divideHashes(std::string initial) {
        std::vector<std::string> res;
        size_t hexHashLen = 64;
        auto start = initial.begin();
        int inc = 0;

        while (inc < 6) {
            auto tmp = std::string(start, start + hexHashLen);
            res.push_back(tmp);
            start = start + hexHashLen;
            ++inc;
        }
        return res;
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
        auto writtenHashesDivided = divideHashes(writtenHashes);
        auto currentHashesDivided = divideHashes(currentHashes);
        auto wIt = writtenHashesDivided.begin();
        auto cIt = currentHashesDivided.begin();
        auto nIt = hashesDivisions.begin();
        while (wIt < writtenHashesDivided.end()) {
            if (*wIt != *cIt) {
                csinfo() << *nIt << " current: "
                    << *cIt
                    << ", written: "
                    << *wIt;

            }
            ++wIt;
            ++cIt;
            ++nIt;
        }
        return currentHashes == writtenHashes;
    }

    std::set<size_t> getVersions() {
        std::set<size_t> result;

        for (auto& p : std::filesystem::directory_iterator(kQuickStartRoot)) {
            auto path = p.path().string();
            if (path.empty()) {
                continue;
            }
            std::replace(path.begin(), path.end(), '\\', '/');

            if (path.back() == '/') {
                path.pop_back();
            }
            auto stringVersion = path.substr(path.rfind('/') + 1);

            try {
                result.insert(stoll(stringVersion));
            }
            catch (...) {
                cserror() << "CachesSerializationManager: cannot get version from " << path
                          << ", " << stringVersion;
            }
        }

        return result;
    }

    bool loadVersion(size_t version) {
        csinfo() << "CachesSerializationManager: try to load version " << version;
        try {
            std::filesystem::path p(kQuickStartRoot);
            p /= std::to_string(version);

            blockchainSerializer.load(p);
            csinfo() << "Blockchain settings: loaded";
            smartContractsSerializer.load(p);
            csinfo() << "Smart-contracts: loaded";
            walletsCacheSerializer.load(p);
            csinfo() << "Wallets: loaded";
            walletsIdsSerializer.load(p);
            csinfo() << "Wallets Ids: loaded";
#ifdef NODE_API
            tokensMasterSerializer.load(p);
            csinfo() << "Tokens: loaded";
            apiHandlerSerializer.load(p);
            csinfo() << "API handler: loaded";
#endif
            if (!checkHashes(version)) {
                cserror() << "CachesSerializationManager: invalid hashes on load";
                clear(version);
                return false;
            }
        } catch (const std::exception& e) {
            cserror() << "CachesSerializationManager: error on load: "
                      << e.what();
            clear(version);
            return false;
        } catch (...) {
            cserror() << "CachesSerializationManager: unknown error on load";
            clear(version);
            return false;
        }
        return true;
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

void CachesSerializationManager::bind(BlockChain& bc, std::set<cs::PublicKey>& initialConfidants) {
    pImpl_->blockchainSerializer.bind(bc, initialConfidants);
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

void CachesSerializationManager::bind([[maybe_unused]] api::APIHandler& apih) {
#ifdef NODE_API
    pImpl_->apiHandlerSerializer.bind(apih);
    pImpl_->bindFlags |= (1 << Impl::APIHandlerBit);
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
        pImpl_->apiHandlerSerializer.save(p);
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

bool CachesSerializationManager::load() {
    if (!pImpl_->bindingsReady()) {
        cserror() << "CachesSerializationManager: load error: "
                  << "bindings are not ready";
        return false;
    }

    // try to load most recent version first
    if (pImpl_->loadVersion(0)) {
        csinfo() << "CachesSerializationManager: successfully load version 0";
        return true;
    }

    auto versions = pImpl_->getVersions();

    // load versions starting from greatest numbers
    for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
        if (pImpl_->loadVersion(*it)) {
            csinfo() << "CachesSerializationManager: successfully load version " << *it;
            return true;
        }
    }

    cserror() << "CachesSerializationManager: no suitable version found";
    return false;
}

void CachesSerializationManager::clear(size_t version) {
    if (!pImpl_->bindingsReady()) {
        return;
    }
    pImpl_->clear(version);
}

} // namespace cs
