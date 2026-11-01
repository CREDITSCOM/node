#include <csnode/caches_serialization_manager.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#ifdef _WIN32
#  include <io.h>
#  include <fcntl.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

#include <csnode/blockchain_serializer.hpp>
#include <csnode/smartcontracts_serializer.hpp>
#include <csnode/tokens_serializer.hpp>
#include <csnode/walletscache_serializer.hpp>
#include <csnode/walletsids_serializer.hpp>
#include <csnode/apihandler_serializer.hpp>
#include <csnode/roundstat_serializer.hpp>
#include <csconnector/csconnector.hpp>

#include <lib/system/logger.hpp>

namespace {

void fsyncFile(const std::filesystem::path& p) {
#ifdef _WIN32
    int fd = ::_wopen(p.wstring().c_str(), _O_RDONLY | _O_BINARY);
    if (fd >= 0) {
        ::_commit(fd);
        ::_close(fd);
    }
#else
    int fd = ::open(p.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif
}

void fsyncDir([[maybe_unused]] const std::filesystem::path& dir) {
#ifndef _WIN32
    int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif
}

void fsyncDirContents(const std::filesystem::path& dir) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec)) {
            fsyncFile(entry.path());
        }
    }
}

} // namespace

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
    RoundStat_Serializer roundStatSerializer;


    const std::string kHashesFile = "quick_start_hashes.dat";
    const std::string kQuickStartRoot = "qs";
    const std::string kHeadFile = "head.bin";
    const std::string kSchemaFile = "schema_version";
    const std::string kSentinelFile = "sentinel.bin";
    static constexpr uint8_t kCurrentSchemaVersion = 1;
    static constexpr uint32_t kHeadMagic = 0x44414548u; // "HEAD" LE
    static constexpr uint8_t kSentinelCompletedFromGenesisBit = 1 << 0;
    const std::vector <std::string> hashesDivisions = {"blockchain", "smartcontracts","walletscache","walletsIds","roundstat","tokensmaster","apihandler"};

    std::optional<CheckpointHead> loadedHead_{};
    std::optional<size_t> loadedVersion_{};
    bool completedFromGenesis_ = false;
    bool loadedFromCompleted_ = false;

    enum BindBits {
      BlockChainBit,
      SmartContractsBit,
      WalletsCacheBit,
      WalletsIdsBit,
      RoundStatBit
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
            (bindFlags & (1 << WalletsIdsBit)) &&
            (bindFlags & (1 << RoundStatBit))
#ifdef NODE_API
            && (bindFlags & (1 << TokensMasterBit))
            && (bindFlags & (1 << APIHandlerBit))
#endif
        );
    }

    void clearInMem() {
        std::filesystem::path unused;
        try {
            blockchainSerializer.clear(unused);
            smartContractsSerializer.clear(unused);
#ifdef NODE_API
            tokensMasterSerializer.clear(unused);
            apiHandlerSerializer.clear(unused);
#endif
            walletsCacheSerializer.clear(unused);
            walletsIdsSerializer.clear(unused);
            roundStatSerializer.clear(unused);
        }
        catch (const std::exception& e) {
            cswarning() << "CachesSerializationManager: error on clearInMem: " << e.what();
        }
    }

    void quarantineVersion(size_t version) {
        std::filesystem::path p(kQuickStartRoot);
        p /= std::to_string(version);
        std::filesystem::path q(kQuickStartRoot);
        q /= (std::to_string(version) + ".bad");
        std::error_code ec;
        std::filesystem::remove_all(q, ec);
        std::filesystem::rename(p, q, ec);
        if (ec) {
            cswarning() << "CachesSerializationManager: quarantine rename "
                        << p << " -> " << q << " failed: " << ec.message()
                        << "; removing failed version dir";
            std::filesystem::remove_all(p, ec);
        }
        else {
            csinfo() << "CachesSerializationManager: quarantined failed version " << version
                     << " to " << q;
        }
    }

    void clear(size_t version) {
        csinfo() << "CachesSerializationManager: try to clear version " << version;
        clearInMem();
        std::filesystem::path p(kQuickStartRoot);
        p /= std::to_string(version);
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
        if (ec) {
            cswarning() << "CachesSerializationManager: remove_all "
                        << p << " failed: " << ec.message();
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
      addHash(result, roundStatSerializer);
      csdebug() << "Got roundStat hashes";
#ifdef NODE_API
      addHash(result, tokensMasterSerializer);
      csdebug() << "Got tokensmaster hashes";
      addHash(result, apiHandlerSerializer);
      csdebug() << "Got apihandler hashes";
#endif

      auto hex = cscrypto::helpers::bin2Hex(
        result.data(),
        result.size()
      );
      while (!hex.empty() && hex.back() == '\0') hex.pop_back();   // strip libsodium's trailing null
      return hex;
    }

    void saveHashes(const std::filesystem::path& dir) {
        std::ofstream f(dir / kHashesFile);
        f << getHashes();
    }

    void saveSchema(const std::filesystem::path& dir) {
        std::ofstream f(dir / kSchemaFile, std::ios::binary);
        const uint8_t v = kCurrentSchemaVersion;
        f.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }

    // Returns the schema version, or 0 if no schema file (legacy checkpoint).
    uint8_t loadSchema(const std::filesystem::path& dir) {
        std::ifstream f(dir / kSchemaFile, std::ios::binary);
        if (!f.is_open()) return 0;
        uint8_t v = 0;
        f.read(reinterpret_cast<char*>(&v), sizeof(v));
        return f ? v : 0;
    }

    void saveHead(const std::filesystem::path& dir, const CheckpointHead& head) {
        std::ofstream f(dir / kHeadFile, std::ios::binary);
        const uint32_t magic = kHeadMagic;
        f.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        const uint64_t seq = static_cast<uint64_t>(head.sequence);
        f.write(reinterpret_cast<const char*>(&seq), sizeof(seq));
        const uint16_t hh = static_cast<uint16_t>(head.head_hash.size());
        f.write(reinterpret_cast<const char*>(&hh), sizeof(hh));
        if (hh) f.write(reinterpret_cast<const char*>(head.head_hash.data()), hh);
        const uint16_t ph = static_cast<uint16_t>(head.prev_hash.size());
        f.write(reinterpret_cast<const char*>(&ph), sizeof(ph));
        if (ph) f.write(reinterpret_cast<const char*>(head.prev_hash.data()), ph);
    }

    void saveSentinel(const std::filesystem::path& dir) {
        std::ofstream f(dir / kSentinelFile, std::ios::binary);
        uint8_t flags = 0;
        if (completedFromGenesis_) flags |= kSentinelCompletedFromGenesisBit;
        f.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
    }

    // returns false for missing/legacy files
    bool loadSentinel(const std::filesystem::path& dir) {
        std::ifstream f(dir / kSentinelFile, std::ios::binary);
        if (!f.is_open()) return false;
        uint8_t flags = 0;
        f.read(reinterpret_cast<char*>(&flags), sizeof(flags));
        if (!f) return false;
        return (flags & kSentinelCompletedFromGenesisBit) != 0;
    }

    std::optional<CheckpointHead> loadHead(const std::filesystem::path& dir) {
        std::ifstream f(dir / kHeadFile, std::ios::binary);
        if (!f.is_open()) return std::nullopt;
        uint32_t magic = 0;
        f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (!f || magic != kHeadMagic) {
            cswarning() << "CachesSerializationManager: head.bin magic mismatch in " << dir;
            return std::nullopt;
        }
        CheckpointHead head;
        uint64_t seq = 0;
        f.read(reinterpret_cast<char*>(&seq), sizeof(seq));
        head.sequence = static_cast<cs::Sequence>(seq);
        uint16_t hh = 0;
        f.read(reinterpret_cast<char*>(&hh), sizeof(hh));
        if (hh) {
            head.head_hash.resize(hh);
            f.read(reinterpret_cast<char*>(head.head_hash.data()), hh);
        }
        uint16_t ph = 0;
        f.read(reinterpret_cast<char*>(&ph), sizeof(ph));
        if (ph) {
            head.prev_hash.resize(ph);
            f.read(reinterpret_cast<char*>(head.prev_hash.data()), ph);
        }
        if (!f) {
            cswarning() << "CachesSerializationManager: head.bin truncated in " << dir;
            return std::nullopt;
        }
        return head;
    }

    std::vector<std::string> divideHashes(const std::string& initial) const {
        std::vector<std::string> res;
        constexpr size_t hexHashLen = 64;
        if (initial.size() % hexHashLen != 0) {
            cswarning() << "CachesSerializationManager: hash string length " << initial.size()
                        << " is not a multiple of " << hexHashLen;
            return res;
        }
        const size_t count = initial.size() / hexHashLen;
        if (count > hashesDivisions.size()) {
            cswarning() << "CachesSerializationManager: too many hashes (" << count
                        << "), max expected " << hashesDivisions.size();
            return res;
        }
        res.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            res.emplace_back(initial, i * hexHashLen, hexHashLen);
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
        while (!writtenHashes.empty() && writtenHashes.back() == '\0') writtenHashes.pop_back();   // legacy files had trailing null
        if (writtenHashes.empty()) {
            cserror() << "CachesSerializationManager: hash file empty or missing for version " << version;
            return false;
        }
        if (writtenHashes.size() != currentHashes.size()) {
            cserror() << "CachesSerializationManager: hash file size mismatch (build/schema drift), written="
                      << writtenHashes.size() << " current=" << currentHashes.size();
            return false;
        }
        auto writtenHashesDivided = divideHashes(writtenHashes);
        auto currentHashesDivided = divideHashes(currentHashes);
        if (writtenHashesDivided.size() != currentHashesDivided.size()
            || writtenHashesDivided.empty()) {
            cserror() << "CachesSerializationManager: divided hash count mismatch";
            return false;
        }
        auto wIt = writtenHashesDivided.begin();
        auto cIt = currentHashesDivided.begin();
        auto nIt = hashesDivisions.begin();
        while (wIt != writtenHashesDivided.end() && nIt != hashesDivisions.end()) {
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
            const auto ext = p.path().extension();
            if (ext == ".tmp" || ext == ".bad" || ext == ".prev") {
                continue;
            }
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

    bool loadVersion(size_t version, const std::function<bool(const CheckpointHead&)>& verify) {
        csinfo() << "CachesSerializationManager: try to load version " << version;
        try {
            std::filesystem::path p(kQuickStartRoot);
            p /= std::to_string(version);

            const uint8_t schema = loadSchema(p);
            if (schema != 0 && schema != kCurrentSchemaVersion) {
                cserror() << "CachesSerializationManager: schema_version mismatch for "
                          << version << " (file=" << static_cast<int>(schema)
                          << " current=" << static_cast<int>(kCurrentSchemaVersion)
                          << "); quarantining";
                clearInMem();
                quarantineVersion(version);
                return false;
            }

            blockchainSerializer.load(p);
            csinfo() << "Blockchain settings: loaded";
            smartContractsSerializer.load(p);
            csinfo() << "Smart-contracts: loaded";
            walletsCacheSerializer.load(p);
            csinfo() << "Wallets: loaded";
            walletsIdsSerializer.load(p);
            csinfo() << "Wallets Ids: loaded";
            roundStatSerializer.load(p);
            csinfo() << "Wallets Ids: loaded";
#ifdef NODE_API
            tokensMasterSerializer.load(p);
            csinfo() << "Tokens: loaded";
            apiHandlerSerializer.load(p);
            csinfo() << "API handler: loaded";
#endif
            if (!checkHashes(version)) {
                cserror() << "CachesSerializationManager: invalid hashes on load, quarantining version " << version;
                clearInMem();
                quarantineVersion(version);
                return false;
            }
            loadedHead_ = loadHead(p);   // nullopt for legacy (no head.bin)
            loadedFromCompleted_ = loadSentinel(p);
            // a completed snapshot remains completed after a successful reload
            if (loadedFromCompleted_) completedFromGenesis_ = true;
            if (verify && loadedHead_.has_value() && !verify(*loadedHead_)) {
                cserror() << "CachesSerializationManager: chain-binding verification failed for version "
                          << version << "; quarantining";
                loadedHead_.reset();
                loadedFromCompleted_ = false;
                clearInMem();
                quarantineVersion(version);
                return false;
            }
        } catch (const std::exception& e) {
            cserror() << "CachesSerializationManager: error on load: "
                      << e.what() << "; quarantining version " << version;
            clearInMem();
            quarantineVersion(version);
            return false;
        } catch (...) {
            cserror() << "CachesSerializationManager: unknown error on load; quarantining version " << version;
            clearInMem();
            quarantineVersion(version);
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
    return;
  }
  // Pass 1: clean .tmp; collect .prev for crash-mid-publish recovery
  std::vector<std::filesystem::path> prevDirs;
  for (auto& p : std::filesystem::directory_iterator(pImpl_->kQuickStartRoot)) {
    const auto ext = p.path().extension();
    if (ext == ".tmp") {
      std::error_code ec;
      std::filesystem::remove_all(p.path(), ec);
    }
    else if (ext == ".prev") {
      prevDirs.push_back(p.path());
    }
    // .bad dirs are preserved across boots for forensic inspection
  }
  // Pass 2: recover .prev if final dir is missing; otherwise drop the backup.
  for (const auto& prev : prevDirs) {
    std::filesystem::path final_path = prev;
    final_path.replace_extension();
    std::error_code ec;
    if (std::filesystem::exists(final_path, ec)) {
      std::filesystem::remove_all(prev, ec);
    }
    else {
      std::filesystem::rename(prev, final_path, ec);
      if (ec) {
        cswarning() << "CachesSerializationManager: recover " << prev
                    << " -> " << final_path << " failed: " << ec.message();
      }
      else {
        csinfo() << "CachesSerializationManager: recovered prior version from "
                 << prev << " -> " << final_path;
      }
    }
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

void CachesSerializationManager::bind(RoundStat& rs) {
    pImpl_->roundStatSerializer.bind(rs);
    pImpl_->bindFlags |= (1 << Impl::RoundStatBit);
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
    return save(version, CheckpointHead{});
}

bool CachesSerializationManager::save(size_t version, const CheckpointHead& head) {
    if (!pImpl_->bindingsReady()) {
        cserror() << "CachesSerializationManager: save error: "
                  << "bindings are not ready";
        return false;
    }

    const std::filesystem::path root(pImpl_->kQuickStartRoot);
    const std::filesystem::path final_path = root / std::to_string(version);
    const std::filesystem::path tmp_path   = root / (std::to_string(version) + ".tmp");

    std::error_code ec;
    std::filesystem::remove_all(tmp_path, ec);
    std::filesystem::create_directories(tmp_path, ec);
    if (ec) {
        cserror() << "CachesSerializationManager: cannot create " << tmp_path << ": " << ec.message();
        return false;
    }

    try {
        pImpl_->blockchainSerializer.save(tmp_path);
        pImpl_->smartContractsSerializer.save(tmp_path);
        pImpl_->walletsCacheSerializer.save(tmp_path);
        pImpl_->walletsIdsSerializer.save(tmp_path);
        pImpl_->roundStatSerializer.save(tmp_path);
#ifdef NODE_API
        pImpl_->tokensMasterSerializer.save(tmp_path);
        pImpl_->apiHandlerSerializer.save(tmp_path);
#endif
        pImpl_->saveHashes(tmp_path);
        pImpl_->saveSchema(tmp_path);
        pImpl_->saveSentinel(tmp_path);
        if (!head.head_hash.empty()) {
            pImpl_->saveHead(tmp_path, head);
        }
    } catch (const std::exception& e) {
        cserror() << "CachesSerializationManager: error on save: " << e.what();
        std::filesystem::remove_all(tmp_path, ec);
        return false;
    } catch (...) {
        cserror() << "CachesSerializationManager: unknown save error ";
        std::filesystem::remove_all(tmp_path, ec);
        return false;
    }

    // ensure data is on disk before we publish via rename
    fsyncDirContents(tmp_path);

    // .prev rename-over-replace: prior version preserved until new one is published
    const std::filesystem::path backup_path = root / (std::to_string(version) + ".prev");
    std::filesystem::remove_all(backup_path, ec);

    const bool hadOldFinal = std::filesystem::exists(final_path, ec);
    if (hadOldFinal) {
        std::filesystem::rename(final_path, backup_path, ec);
        if (ec) {
            cserror() << "CachesSerializationManager: backup rename "
                      << final_path << " -> " << backup_path << " failed: " << ec.message();
            std::filesystem::remove_all(tmp_path, ec);
            return false;
        }
    }

    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        cserror() << "CachesSerializationManager: publish rename "
                  << tmp_path << " -> " << final_path << " failed: " << ec.message();
        if (hadOldFinal) {
            std::error_code restoreEc;
            std::filesystem::rename(backup_path, final_path, restoreEc);
            if (restoreEc) {
                cswarning() << "CachesSerializationManager: could not restore prior version "
                            << backup_path << " -> " << final_path << ": " << restoreEc.message();
            }
        }
        std::filesystem::remove_all(tmp_path, ec);
        return false;
    }

    // make rename itself durable (POSIX requires fsync of the parent dir)
    fsyncDir(root);

    // publish succeeded — drop the backup
    std::filesystem::remove_all(backup_path, ec);
    return true;
}

bool CachesSerializationManager::load() {
    return load(ChainVerifier{});
}

bool CachesSerializationManager::load(const ChainVerifier& verify) {
    if (!pImpl_->bindingsReady()) {
        cserror() << "CachesSerializationManager: load error: "
                  << "bindings are not ready";
        return false;
    }

    pImpl_->loadedHead_.reset();
    pImpl_->loadedVersion_.reset();
    pImpl_->loadedFromCompleted_ = false;

    // Rank by max(head.seq, dir_name); mtime tiebreaker. Legacy accepted.
    struct Candidate {
        cs::Sequence headSeq = 0;
        cs::Sequence effectiveSeq = 0;
        std::filesystem::file_time_type mtime{};
        size_t version = 0;
        bool hasHead = false;
    };

    auto versions = pImpl_->getVersions();
    std::vector<Candidate> candidates;
    candidates.reserve(versions.size());
    for (size_t v : versions) {
        std::filesystem::path p(pImpl_->kQuickStartRoot);
        p /= std::to_string(v);
        Candidate c;
        c.version = v;
        auto head = pImpl_->loadHead(p);
        if (head) {
            c.headSeq = head->sequence;
            c.hasHead = true;
        }
        c.effectiveSeq = std::max<cs::Sequence>(c.headSeq, static_cast<cs::Sequence>(v));
        std::error_code ec;
        auto sentinelPath = p / pImpl_->kSentinelFile;
        if (std::filesystem::exists(sentinelPath, ec)) {
            c.mtime = std::filesystem::last_write_time(sentinelPath, ec);
        } else {
            c.mtime = std::filesystem::last_write_time(p, ec);
        }
        candidates.push_back(c);
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.effectiveSeq != b.effectiveSeq) return a.effectiveSeq > b.effectiveSeq;
        return a.mtime > b.mtime;
    });

    if (candidates.empty()) {
        cserror() << "CachesSerializationManager: no checkpoint candidates";
        return false;
    }

    csinfo() << "CachesSerializationManager: checkpoint candidates (in order tried):";
    for (const auto& c : candidates) {
        csinfo() << "  qs/" << c.version
                 << "  head.seq=" << (c.hasHead ? std::to_string(c.headSeq) : std::string("<legacy>"))
                 << "  effective=" << c.effectiveSeq;
    }

    for (const auto& c : candidates) {
        if (pImpl_->loadVersion(c.version, verify)) {
            pImpl_->loadedVersion_ = c.version;
            csinfo() << "CachesSerializationManager: successfully loaded qs/" << c.version
                     << " (effective seq=" << c.effectiveSeq << ")";
            return true;
        }
    }

    cserror() << "CachesSerializationManager: no suitable version found";
    return false;
}

std::optional<CheckpointHead> CachesSerializationManager::getLoadedHead() const {
    return pImpl_->loadedHead_;
}

std::optional<size_t> CachesSerializationManager::getLoadedVersion() const {
    return pImpl_->loadedVersion_;
}

void CachesSerializationManager::quarantine(size_t version) {
    pImpl_->quarantineVersion(version);
}

void CachesSerializationManager::setCompletedFromGenesis() {
    pImpl_->completedFromGenesis_ = true;
}

bool CachesSerializationManager::isLoadedFromCompletedSnapshot() const {
    return pImpl_->loadedFromCompleted_;
}

void CachesSerializationManager::clear(size_t version) {
    if (!pImpl_->bindingsReady()) {
        return;
    }
    pImpl_->clear(version);
}

void CachesSerializationManager::pruneCheckpoints(size_t keep) {
    if (!pImpl_->bindingsReady()) {
        return;
    }
    auto versions = pImpl_->getVersions();
    versions.erase(0);
    if (versions.size() <= keep) {
        return;
    }
    // Prune by mtime so recent saves survive over high-number legacy.
    struct V {
        size_t version;
        std::filesystem::file_time_type mtime;
    };
    std::vector<V> ordered;
    ordered.reserve(versions.size());
    for (size_t v : versions) {
        std::filesystem::path p(pImpl_->kQuickStartRoot);
        p /= std::to_string(v);
        std::error_code ec;
        auto sentinel = p / pImpl_->kSentinelFile;
        auto mt = std::filesystem::exists(sentinel, ec)
            ? std::filesystem::last_write_time(sentinel, ec)
            : std::filesystem::last_write_time(p, ec);
        ordered.push_back({v, mt});
    }
    // Newest mtime first; keep the first `keep`, prune the rest.
    std::sort(ordered.begin(), ordered.end(),
              [](const V& a, const V& b) { return a.mtime > b.mtime; });

    for (size_t i = keep; i < ordered.size(); ++i) {
        std::filesystem::path p(pImpl_->kQuickStartRoot);
        p /= std::to_string(ordered[i].version);
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
        if (ec) {
            cswarning() << "CachesSerializationManager: prune failed for " << p << ": " << ec.message();
        } else {
            csdebug() << "CachesSerializationManager: pruned old qs/" << ordered[i].version;
        }
    }
}

} // namespace cs
