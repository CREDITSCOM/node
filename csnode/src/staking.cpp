#include <staking.hpp>

#include <algorithm>
#include <stdexcept>

#include <lib/system/logger.hpp>

namespace cs {

namespace {
auto zeroTimeStampPredicate = [](cs::TimeMoney& tm) { return tm.time == cs::Zero::timeStamp; };
} // namespace

Staking::Staking(
    GetWalletDataCallback getCb,
    WalletUpdatedCallback updateCb
) : getWalletData_(getCb)
  , updateCallback_(updateCb) {
    if (!getWalletData_) {
        throw std::runtime_error("Staking: nullptr in getWalletData_");
    }
    if (!updateCallback_) {
        throw std::runtime_error("Staking: nullptr in updateCallback_");
    }
}

void Staking::cleanObsoletteDelegations(uint64_t time) {
    uint64_t delTime = time / 1000;
    auto it = currentDelegations_.begin();
    while (it != getCurrentDelegations().end() && it->first < delTime) {
        cleanDelegationsFromCache(delTime, it->second);
        it = currentDelegations_.erase(it);
    }

    for (auto it = miningDelegations_.begin(); it != miningDelegations_.end(); ) {
      it->second.erase(
          std::remove_if(it->second.begin(), it->second.end(), [delTime](const auto& keyAndTimemoney) {
              return keyAndTimemoney.second.time < delTime;
          }),
          it->second.end()
      );

      if (it->second.empty()) {
        it = miningDelegations_.erase(it);
      }
      else {
        ++it;
      }
    }
}

void Staking::cleanDelegationsFromCache(uint64_t delTime, Delegations& value) {
    for (auto& it : value) {
        auto sourceWallData = getWalletData_(std::get<0>(it));
        auto targetWallData = getWalletData_(std::get<1>(it));
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

        updateCallback_(sourceWallData);
        updateCallback_(targetWallData);
    }
}

bool Staking::removeSingleDelegation(
    uint64_t delTime,
    const PublicKey& first,
    const PublicKey& second,
    csdb::TransactionID id
) {
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
        auto sourceWallData = getWalletData_(first);
        auto targetWallData = getWalletData_(second);
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
        updateCallback_(sourceWallData);
        updateCallback_(targetWallData);
    }
    return true;
}

void Staking::addDelegationsForTarget(
    const csdb::UserField& ufld,
    const PublicKey& sKey,
    const PublicKey& tKey,
    const csdb::Amount& amount,
    const csdb::TransactionID& trx_id
) {
    WalletsCache::WalletData targetWallet = getWalletData_(tKey);

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
        if (tm.time >= kMinDelegationForMiningSeconds_) {
          miningDelegations_[tKey].push_back({sKey, tm});
        }
    }
    else {
        cserror() << "Staking: error in addDelegations, pool num: "
                  << trx_id.pool_seq()
                  << ", trx num: " << trx_id.index();
    }

    if (timeMoneyVector.empty()) {
        targetWallet.delegateSources_->erase(sKey);
    }
    updateCallback_(targetWallet);
}

void Staking::revertDelegationsForTarget(
    const csdb::UserField& ufld,
    const PublicKey& sKey,
    const PublicKey& tKey,
    const csdb::Amount& amount,
    const csdb::TransactionID& trx_id
) {
    WalletsCache::WalletData targetWallet = getWalletData_(tKey);

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
    updateCallback_(targetWallet);
}

void Staking::addDelegationsForSource(
    const csdb::UserField& ufld,
    const PublicKey& sKey,
    const PublicKey& tKey,
    const csdb::Amount& amount
) {
    auto wallData = getWalletData_(sKey);
    if (wallData.delegateTargets_ == nullptr) {
        wallData.delegateTargets_ = std::make_shared<std::map<cs::PublicKey, std::vector<cs::TimeMoney>>>();
    }
    auto it = wallData.delegateTargets_->find(tKey);
    if (ufld.value<uint64_t>() == trx_uf::sp::de::legate) {
        wallData.balance_ -= amount;
        cs::TimeMoney tm(cs::Zero::timeStamp, amount);
        if (it == wallData.delegateTargets_->end()) {
            std::vector<cs::TimeMoney> firstElement;
            firstElement.push_back(tm);
            wallData.delegateTargets_->emplace(tKey, firstElement);
        }
        else {
            auto itt = std::find_if(it->second.begin(), it->second.end(), [](cs::TimeMoney& tm) {return tm.time == cs::Zero::timeStamp; });
            if (itt == it->second.end()) {
                it->second.push_back(tm);
            }
            else {
                itt->amount += amount;
            }
        }
    }
    else if (ufld.value<uint64_t>() == trx_uf::sp::de::legated_withdraw) {
        if (it != wallData.delegateTargets_->end()) {
            auto itt = std::find_if(it->second.begin(), it->second.end(), [](cs::TimeMoney& tm) {return tm.time == cs::Zero::timeStamp; });
            if (itt != it->second.end()) {
                itt->amount -= amount;
                wallData.balance_ += amount;
                //removing empty records
                if (itt->amount == csdb::Amount{ 0 }) {
                    it->second.erase(itt);
                    if (it->second.size() == 0U) {
                        wallData.delegateTargets_->erase(tKey);
                    }
                }
            }
        }
    }
    else if (ufld.value<uint64_t>() >= trx_uf::sp::de::legate_min_utc) {
        cs::TimeMoney tm(ufld.value<uint64_t>() , amount);
        wallData.balance_ -= amount;
        if (it == wallData.delegateTargets_->end()) {
            std::vector<cs::TimeMoney> firstElement;
            firstElement.push_back(tm);
            wallData.delegateTargets_->emplace(tKey, firstElement);
        }
        else {
            it->second.push_back(tm);
        }
    }
    else {
        cserror() << "Staking: error addDelegationsForSource";
    }
    updateCallback_(wallData);
}

void Staking::revertDelegationsForSource(
    const csdb::UserField& ufld,
    const PublicKey& sKey,
    const PublicKey& tKey,
    const csdb::Amount& amount,
    const csdb::TransactionID& trx_id
) {
    auto wallData = getWalletData_(sKey);
    if (wallData.delegateTargets_ == nullptr) {
        wallData.delegateTargets_ = std::make_shared<std::map<cs::PublicKey, std::vector<cs::TimeMoney>>>();
    }
    //delegate transaction(inverse)
    auto it = wallData.delegateTargets_->find(tKey);
    if (ufld.value<uint64_t>() == trx_uf::sp::de::legate) {
        if (it != wallData.delegateTargets_->end()) {
            auto itt = std::find_if(it->second.begin(), it->second.end(), [](cs::TimeMoney& tm) {return tm.time == cs::Zero::timeStamp; });
            if (itt != it->second.end()) {
                itt->amount -= amount;
                wallData.balance_ += amount;
                //removing empty records
                if (itt->amount == csdb::Amount{ 0 }) {
                    it->second.erase(itt);
                    if (it->second.size() == 0U) {
                        wallData.delegateTargets_->erase(tKey);
                    }
                }
            }
        }
    }
    //withdraw delegation (inverse)
    else if (ufld.value<uint64_t>() == trx_uf::sp::de::legated_withdraw) {
        wallData.balance_ -= amount;
        cs::TimeMoney tm(cs::Zero::timeStamp, amount);
        if (it == wallData.delegateTargets_->end()) {
            std::vector<cs::TimeMoney> firstElement;
            firstElement.push_back(tm);
            wallData.delegateTargets_->emplace(tKey, firstElement);
        }
        else {
            auto itt = std::find_if(it->second.begin(), it->second.end(), [](cs::TimeMoney& tm) {return tm.time == cs::Zero::timeStamp; });
            if (itt == it->second.end()) {
                it->second.push_back(tm);
            }
            else {
                itt->amount += amount;
            }
        }
    }
    else if (ufld.value<uint64_t>() >= trx_uf::sp::de::legate_min_utc) {
        //nothing to do if time of this transacton is already finished
        removeSingleDelegation(ufld.value<uint64_t>(), sKey, tKey, trx_id);
    }
    else {
        cserror() << "Staking: error revertDelegationsForSource";
    }
    updateCallback_(wallData);
}
} // namespace cs
