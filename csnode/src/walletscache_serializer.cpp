#include <fstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <csnode/walletscache.hpp>
#include <csnode/walletscache_serializer.hpp>

#include <csnode/multiwallets.hpp>
#include <csnode/staking.hpp>

namespace cs {
void WalletsCache_Serializer::bind(WalletsCache& wCache) {
    smartPayableTransactions_ =
        reinterpret_cast<decltype(smartPayableTransactions_)>(&wCache.smartPayableTransactions_);
    canceledSmarts_ = reinterpret_cast<decltype(canceledSmarts_)>(&wCache.canceledSmarts_);
    wallets_ = reinterpret_cast<decltype(wallets_)>(&wCache.multiWallets_->indexes_);
#ifdef MONITOR_NODE
    trusted_info_ = reinterpret_cast<decltype(trusted_info_)>(&wCache.trusted_info_);
#endif
    currentDelegations_ = reinterpret_cast<decltype(currentDelegations_)>(&wCache.staking_->currentDelegations_);
    miningDelegations_ = reinterpret_cast<decltype(miningDelegations_)>(&wCache.staking_->miningDelegations_);
}

void WalletsCache_Serializer::clear() {
    smartPayableTransactions_->clear();
    canceledSmarts_->clear();
    wallets_->clear();
#ifdef MONITOR_NODE
    trusted_info_->clear();
#endif
    currentDelegations_->clear();
    miningDelegations_->clear();
    save();
}

void WalletsCache_Serializer::save() {
    std::ofstream ofs("walletscache.dat");
    boost::archive::text_oarchive oa(ofs);
    oa << *smartPayableTransactions_;
    oa << *canceledSmarts_;
    oa << *wallets_;
#ifdef MONITOR_NODE
    oa << *trusted_info_;
#endif
    oa << *currentDelegations_;
    oa << *miningDelegations_;
}

void WalletsCache_Serializer::load() {
    std::ifstream ifs("walletscache.dat");
    boost::archive::text_iarchive ia(ifs);
    ia >> *smartPayableTransactions_;
    ia >> *canceledSmarts_;
    ia >> *wallets_;
#ifdef MONITOR_NODE
    ia >> *trusted_info_;
#endif
    ia >> *currentDelegations_;
    ia >> *miningDelegations_;
}
}  // namespace cs
