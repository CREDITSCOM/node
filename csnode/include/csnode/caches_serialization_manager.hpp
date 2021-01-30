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

    void bind(
        BlockChain&,
        SmartContracts&,
        TokensMaster&,
        WalletsCache&,
        WalletsIds&
    );

    void save();
    void load();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace cs
