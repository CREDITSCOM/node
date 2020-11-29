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
    return keys.find(key)->balance_;
}

uint64_t cs::MultiWallets::transactionsCount(const cs::PublicKey& key) const {
    cs::Lock lock(mutex_);

    auto& keys = indexes_.get<Tags::ByPublicKey>();
    return keys.find(key)->transNum_;
}

#ifdef MONITOR_NODE
uint64_t cs::MultiWallets::createTime(const cs::PublicKey& key) const {
    cs::Lock lock(mutex_);

    auto& keys = indexes_.get<Tags::ByPublicKey>();
    return keys.find(key)->createTime_;
}
#endif

void cs::MultiWallets::onWalletCacheUpdated(const cs::WalletsCache::WalletData& data) {
    cs::Lock lock(mutex_);
    auto& byKey = indexes_.get<Tags::ByPublicKey>();

    if (auto iter = byKey.find(data.key_); iter != byKey.end()) {
        byKey.replace(iter, data);
    }
    else {
        indexes_.insert(data);
    }
}
