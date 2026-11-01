#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <set>

#include <client/params.hpp>
#include <lib/system/common.hpp>

class BlockChain;
class TokensMaster;

namespace api {

class APIHandler;

} // namespace api

namespace cs {

class SmartContracts;
class WalletsCache;
class WalletsIds;
class RoundStat;

// Binds a checkpoint to a specific chain-DB state. head.bin stores this; on
// load the caller compares against the live chain DB before trusting caches.
struct CheckpointHead {
    cs::Sequence sequence{0};
    cs::Bytes head_hash;
    cs::Bytes prev_hash;
};

class CachesSerializationManager {
public:
    CachesSerializationManager();
    ~CachesSerializationManager();

    void bind(BlockChain&, std::set<cs::PublicKey>& initialConfidants);
    void bind(SmartContracts&);
    void bind(WalletsCache&);
    void bind(WalletsIds&);
    void bind(RoundStat&);
    void bind(TokensMaster&);
    void bind(api::APIHandler&);

    void clear(size_t version = 0);

    bool save(size_t version, const CheckpointHead& head);
    bool save(size_t version = 0);   // legacy: no chain-binding (qs/0 from pre-A nodes)
    // Optional verifier is called after each candidate's hashes pass; if it
    // returns false (e.g. chain-binding mismatch), the candidate is
    // quarantined and the next-older version is tried.
    using ChainVerifier = std::function<bool(const CheckpointHead&)>;
    bool load(const ChainVerifier& verify);
    bool load();
    // The head info that load() recovered, if present. nullopt if the loaded
    // checkpoint is pre-A (no head.bin) — caller skips chain-binding check.
    std::optional<CheckpointHead> getLoadedHead() const;
    // the version load() took the caches from, if any
    std::optional<size_t> getLoadedVersion() const;
    // moves a version aside to qs/<version>.bad, so that load() no longer takes it
    void quarantine(size_t version);
    // Sticky bit indicating the caches have been validated by a full
    // genesis-to-head walk at some point. Cleared on a fresh init, set by
    // BlockChain after Storage::open returns from a successful slow walk.
    void setCompletedFromGenesis();
    bool isLoadedFromCompletedSnapshot() const;
    void pruneCheckpoints(size_t keep);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace cs
