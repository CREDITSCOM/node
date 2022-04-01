#include "csnode/multiwallets.hpp"
#include "lib/system/common.hpp"
#include "csnode/nodecore.hpp"
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <csnode/configholder.hpp>

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

bool cs::MultiWallets::getWalletData(cs::MultiWallets::InternalData& data) const {
  cs::Lock lock(mutex_);

  auto& keys = indexes_.get<Tags::ByPublicKey>();

  auto it = keys.find(data.key_);
  if (it == keys.end()) {
    return false;
  }
  
  data = *it;
  return true;
}

void cs::MultiWallets::onWalletCacheUpdated(const cs::WalletsCache::WalletData& data) {
    //csdebug() << __func__;
    cs::Lock lock(mutex_);
    auto& byKey = indexes_.get<Tags::ByPublicKey>();
    const auto& conf = cs::ConfigHolder::instance().config();
    if (conf->getBalanceChangeFlag() && data.key_ == conf->getBalanceChangeKey()) {
        csdebug() << "Wallet updated: " 
	    << conf->getBalanceChangeAddress()
	    << ", balance: " << data.balance_.to_string() 
	    << ", delegated: " << data.delegated_.to_string(); 
    }

    if (auto iter = byKey.find(data.key_); iter != byKey.end()) {
        byKey.replace(iter, data);
    }
    else {
        indexes_.insert(data);
    }
}

void cs::MultiWallets::iterate(std::function<bool(const PublicKey& key, const InternalData& data)> func) {
    cs::Lock lock(mutex_);
    for (auto it = indexes_.begin(); it != indexes_.end(); ++it) {
        if (!func(it->key_, *it)) {
            break;
        }
    }
}

csdb::Amount cs::MultiWallets::checkWallets() {
    cs::Lock lock(mutex_);
    csdb::Amount total{ 0 };
    for (auto it = indexes_.begin(); it != indexes_.end(); ++it) {
        total += it->balance_;
        total += it->delegated_;
    }
    return total;
}
