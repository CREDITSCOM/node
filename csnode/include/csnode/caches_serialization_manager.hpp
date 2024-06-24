#pragma once

#include <memory>
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

    bool save(size_t version = 0);
    bool load();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace cs
