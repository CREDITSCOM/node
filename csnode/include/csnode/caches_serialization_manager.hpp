#pragma once

#include <memory>

class BlockChain;
class TokensMaster;

namespace cs {

class SmartContracts;
class WalletsCache;
class WalletsIds;

class CachesSerializationManager {
public:
    CachesSerializationManager();
    ~CachesSerializationManager();

    void bind(BlockChain&);
    void bind(SmartContracts&);
    void bind(WalletsCache&);
    void bind(WalletsIds&);
    void bind(TokensMaster&);

    void clear();

    bool save();
    bool load();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace cs
