#include <staking.hpp>

#include <algorithm>

#include <lib/system/logger.hpp>

namespace cs {

void Staking::cleanObsoletteDelegations(uint64_t time) {
    uint64_t delTime = time / 1000;
    auto it = getCurrentDelegations().begin();
    while (it->first < delTime && it != getCurrentDelegations().end()) {
        cleanDelegationsFromCache(delTime, it->second);
        it = getCurrentDelegations().erase(it);
    }
}

void Staking::cleanDelegationsFromCache(uint64_t delTime, Delegations& value) {
    for (auto& it : value) {
        auto& sourceWallData = getWalletData_(std::get<0>(it));
        auto& targetWallData = getWalletData_(std::get<1>(it));
        auto itt = sourceWallData.delegateTargets_->find(std::get<1>(it));
        auto its = targetWallData.delegateSources_->find(std::get<0>(it));
        if (itt != sourceWallData.delegateTargets_->end()) {
            auto shuttle = itt->second.begin();
            while (shuttle != itt->second.end()) {
                if (shuttle->time < delTime && shuttle->time != 0U) {
                    sourceWallData.balance_ += shuttle->amount;
                    shuttle = itt->second.erase(shuttle);
                }
                else {
                    ++shuttle;
                }
            }
            if (itt->second.empty()) {
                sourceWallData.delegateTargets_->erase(itt);
                if (sourceWallData.delegateTargets_->empty()) {
                    sourceWallData.delegateTargets_.reset();
                }
            }
        }
        if (its != targetWallData.delegateSources_->end()) {
            auto shuttle = its->second.begin();
            while (shuttle != its->second.end()) {
                if (shuttle->time < delTime && shuttle->time != 0U) {
                    targetWallData.delegated_ -= shuttle->amount;
                    shuttle = its->second.erase(shuttle);
                }
                else {
                    ++shuttle;
                }
            }
            if (its->second.empty()) {
                targetWallData.delegateSources_->erase(its);
                if (targetWallData.delegateSources_->empty()) {
                    targetWallData.delegateSources_.reset();
                }
            }
        }
    }
}

bool Staking::removeSingleDelegation(uint64_t delTime, PublicKey& first, PublicKey& second, csdb::TransactionID id) {
    auto value = getCurrentDelegations().find(delTime);
    if (value == getCurrentDelegations().end()) {
        cserror() << "Delegated amount couldn't be removed. Could be database inconsistency";
        return false;
    }

    auto current = std::find_if(value->second.begin(), value->second.end(), [id](std::tuple<PublicKey, PublicKey, csdb::TransactionID> iter) {return std::get<2>(iter) == id; });
    if (current != value->second.end()) {
        value->second.erase(current);
    }

    for (auto it : value->second) {
        auto& sourceWallData = getWalletData_(first);
        auto& targetWallData = getWalletData_(second);
        auto itt = sourceWallData.delegateTargets_->find(second);
        auto its = targetWallData.delegateSources_->find(first);
        if (itt != sourceWallData.delegateTargets_->end()) {
            auto shuttle = itt->second.begin();
            while (shuttle != itt->second.end()) {
                if (shuttle->time == delTime ) {
                    sourceWallData.balance_ += shuttle->amount;
                    shuttle = itt->second.erase(shuttle);
                    break;
                }
                else {
                    ++shuttle;
                }
            }
            if (itt->second.empty()) {
                sourceWallData.delegateTargets_->erase(itt);
                if (sourceWallData.delegateTargets_->empty()) {
                    sourceWallData.delegateTargets_.reset();
                }
            }
        }
        if (its != targetWallData.delegateSources_->end()) {
            auto shuttle = its->second.begin();
            while (shuttle != its->second.end()) {
                if (shuttle->time == delTime) {
                    targetWallData.delegated_ -= shuttle->amount;
                    shuttle = its->second.erase(shuttle);
                    break;
                }
                else {
                    ++shuttle;
                }
            }
            if (its->second.empty()) {
                targetWallData.delegateSources_->erase(its);
                if (targetWallData.delegateSources_->empty()) {
                    targetWallData.delegateSources_.reset();
                }
            }
        }
    }
    return true;
}
} // namespace cs
