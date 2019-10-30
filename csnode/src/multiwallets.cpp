#include "csnode/multiwallets.hpp"

bool cs::MultiWallets::contains(const cs::PublicKey& key) const {
    cs::Lock lock(mutex_);

    auto& byKey = indexes_.get<Tags::ByPublicKey>();
    return byKey.find(key) != byKey.end();
}

size_t cs::MultiWallets::size() const {
    cs::Lock lock(mutex_);
    return indexes_.size();
}

csdb::Amount cs::MultiWallets::balance(const cs::PublicKey& key) const {
    cs::Lock lock(mutex_);

    auto& keys = indexes_.get<Tags::ByPublicKey>();
    return keys.find(key)->balance;
}

uint64_t cs::MultiWallets::transactionsCount(const cs::PublicKey& key) const {
    cs::Lock lock(mutex_);

    auto& keys = indexes_.get<Tags::ByPublicKey>();
    return keys.find(key)->transactionsCount;
}

#ifdef MONITOR_NODE
uint64_t cs::MultiWallets::createTime(const cs::PublicKey& key) const {
    cs::Lock lock(mutex_);

    auto& keys = indexes_.get<Tags::ByPublicKey>();
    return keys.find(key)->createTime;
}
#endif

void cs::MultiWallets::onDbReadFinished(const std::unordered_map<cs::PublicKey, cs::WalletsCache::WalletData>& data) {
    cs::Lock lock(mutex_);

    for (const auto& [key, value] : data) {
        auto mapped = map(key, value);
        indexes_.insert(std::move(mapped));
    }
}

void cs::MultiWallets::onWalletCacheUpdated(const cs::PublicKey& key, const cs::WalletsCache::WalletData& data) {
    auto mapped = map(key, data);

    cs::Lock lock(mutex_);
    auto& byKey = indexes_.get<Tags::ByPublicKey>();

    if (auto iter = byKey.find(key); iter != byKey.end()) {
        byKey.replace(iter, mapped);
    }
    else {
        indexes_.insert(mapped);
    }
}

cs::MultiWallets::InternalData cs::MultiWallets::map(const cs::PublicKey& key, const cs::WalletsCache::WalletData& data) {
    InternalData mapped { key, data.balance_, data.transNum_
#ifdef MONITOR_NODE
                                , data.createTime_
#endif
                        };
    return mapped;
}
