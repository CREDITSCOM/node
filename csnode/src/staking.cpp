#include <staking.hpp>

#include <algorithm>
#include <stdexcept>

#include <lib/system/logger.hpp>

namespace cs {

namespace {
auto zeroTimeStampPredicate = [](cs::TimeMoney& tm) { return tm.time == cs::Zero::timeStamp; };
} // namespace

Staking::Staking(GetWalletDataCallback cb) : getWalletData_(cb) {
    if (!getWalletData_) {
        throw std::runtime_error("Staking: nullptr in getWalletData_");
    }
}

void Staking::cleanObsoletteDelegations(uint64_t time) {
    uint64_t delTime = time / 1000;
    auto it = currentDelegations_.begin();
    while (it->first < delTime && it != getCurrentDelegations().end()) {
        cleanDelegationsFromCache(delTime, it->second);
        it = currentDelegations_.erase(it);
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
    auto value = currentDelegations_.find(delTime);
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

void Staking::addDelegations(
    const csdb::UserField& ufld,
    const PublicKey& sKey,
    const PublicKey& tKey,
    const csdb::Amount& amount,
    const csdb::TransactionID& trx_id
) {
    WalletsCache::WalletData& targetWallet = getWalletData_(tKey);

    if (targetWallet.delegateSources_ == nullptr) {
        targetWallet.delegateSources_ = std::make_shared<std::map<cs::PublicKey, std::vector<cs::TimeMoney>>>();
    }

    auto& timeMoneyVector = (*targetWallet.delegateSources_)[sKey];

    if (ufld.value<uint64_t>() == trx_uf::sp::de::legate) {
        cs::TimeMoney tm(cs::Zero::timeStamp, amount);

        auto it = std::find_if(
            timeMoneyVector.begin(),
            timeMoneyVector.end(),
            zeroTimeStampPredicate
        );

        if (it == timeMoneyVector.end()) {
            timeMoneyVector.push_back(tm);
        }
        else {
            it->amount += amount;
        }

        targetWallet.delegated_ += amount;
    }
    else if (ufld.value<uint64_t>() == trx_uf::sp::de::legated_withdraw && !timeMoneyVector.empty()) {
        auto it = std::find_if(
            timeMoneyVector.begin(),
            timeMoneyVector.end(),
            zeroTimeStampPredicate
        );

        if (it != timeMoneyVector.end()) {
            it->amount -= amount;
            targetWallet.delegated_ -= amount;

            //removing empty records
            if (it->amount == csdb::Amount{ 0 }) {
                timeMoneyVector.erase(it);
            }
        }
    }
    else if (ufld.value<uint64_t>() >= trx_uf::sp::de::legate_min_utc) {
        cs::TimeMoney tm(ufld.value<uint64_t>(), amount);
        timeMoneyVector.push_back(tm);
        targetWallet.delegated_ += amount;
        currentDelegations_[tm.time].push_back(std::make_tuple(sKey, tKey, trx_id));
    }
    else {
        cserror() << "Staking: error in addDelegations, pool num: "
                  << trx_id.pool_seq()
                  << ", trx num: " << trx_id.index();
    }

    if (timeMoneyVector.empty()) {
        targetWallet.delegateSources_->erase(sKey);
    }
}

void Staking::revertDelegations(
    const csdb::UserField& ufld,
    const PublicKey& sKey,
    const PublicKey& tKey,
    const csdb::Amount& amount,
    const csdb::TransactionID& trx_id
) {
    WalletsCache::WalletData& targetWallet = getWalletData_(tKey);

    if (targetWallet.delegateSources_ == nullptr) {
        targetWallet.delegateSources_ = std::make_shared<std::map<cs::PublicKey, std::vector<cs::TimeMoney>>>();
    }

    auto& timeMoneyVector = (*targetWallet.delegateSources_)[sKey];

    if (ufld.value<uint64_t>() == trx_uf::sp::de::legate && !timeMoneyVector.empty()) {
        auto it = std::find_if(
            timeMoneyVector.begin(),
            timeMoneyVector.end(),
            zeroTimeStampPredicate
        );

        if (it != timeMoneyVector.end()) {
            it->amount -= amount;
            targetWallet.delegated_ -= amount;

            //removing empty records
            if (it->amount == csdb::Amount{ 0 }) {
                timeMoneyVector.erase(it);
            }
        }
    }
    else if (ufld.value<uint64_t>() == trx_uf::sp::de::legated_withdraw) {
        cs::TimeMoney tm(cs::Zero::timeStamp, amount);

        auto it = std::find_if(
            timeMoneyVector.begin(),
            timeMoneyVector.end(),
            zeroTimeStampPredicate
        );

        if (it == timeMoneyVector.end()) {
            timeMoneyVector.push_back(tm);
        }
        else {
            it->amount += amount;
        }

        targetWallet.delegated_ += amount;
    }
    else if (ufld.value<uint64_t>() >= trx_uf::sp::de::legate_min_utc) {
        cs::TimeMoney tm(ufld.value<uint64_t>(), amount);
        auto it = std::find_if(
          timeMoneyVector.begin(),
          timeMoneyVector.end(),
          [&tm](const cs::TimeMoney& element) { return element.time == tm.time && element.amount == tm.amount; }
        );
        if (it != timeMoneyVector.end()) {
          timeMoneyVector.erase(it);
          targetWallet.delegated_ -= amount;
          currentDelegations_[tm.time].erase(std::find(
            currentDelegations_[tm.time].begin(),
            currentDelegations_[tm.time].end(),
            std::make_tuple(sKey, tKey, trx_id)
          ));
        }
    }
    else {
        cserror() << "Staking: error revertDelegations";
    }

    // @TODO ufld.value<uint64_t>() >= trx_uf::sp::de::legate_min_utc - find out what should be done

    if (timeMoneyVector.empty()) {
        targetWallet.delegateSources_->erase(sKey);
    }
}
} // namespace cs
