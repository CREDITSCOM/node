#include <algorithm>
#include <fstream>
#include <sstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <csnode/walletscache.hpp>
#include <csnode/walletscache_serializer.hpp>

#include <csnode/multiwallets.hpp>
#include <csnode/staking.hpp>

namespace {
const std::string kTmpDataFile = "wcachedata.tmp";
} // namespace

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

void WalletsCache_Serializer::clear(const std::filesystem::path& rootDir) {
    smartPayableTransactions_->clear();
    canceledSmarts_->clear();
    wallets_->clear();
#ifdef MONITOR_NODE
    trusted_info_->clear();
#endif
    currentDelegations_->clear();
    miningDelegations_->clear();
    save(rootDir);
}

void WalletsCache_Serializer::save(const std::filesystem::path& rootDir) {
    std::ofstream ofs(rootDir / "walletscache.dat");
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

::cscrypto::Hash WalletsCache_Serializer::hash() {
    {
      std::ofstream ofs(kTmpDataFile);
      boost::archive::text_oarchive oa(
        ofs,
        boost::archive::no_header | boost::archive::no_codecvt
      );
      oa << *smartPayableTransactions_;
      oa << *canceledSmarts_;
      auto& wallets_data = wallets_->get<1>();
      for (auto& wd : wallets_data) {
          oa << wd;
      }
#ifdef MONITOR_NODE
      oa << *trusted_info_;
#endif
      oa << *currentDelegations_;
      std::map<
        PublicKey,
        std::vector<std::pair<PublicKey, TimeMoney>>
      > tmp_miningDelegations(
        miningDelegations_->begin(),
        miningDelegations_->end()
      );
      oa << tmp_miningDelegations;
    }

    std::vector<uint8_t> buf(::cscrypto::kHashSize + (1 << 30), 0);
    {
      std::ifstream ifs(kTmpDataFile);
      while (!ifs.eof()) {
          ifs.read(
            reinterpret_cast<char*>(buf.data() + ::cscrypto::kHashSize),
            (1 << 30)
          );
          auto hash = ::cscrypto::calculateHash(buf.data(), buf.size());
          std::copy(hash.begin(), hash.end(), buf.begin());
      }
    }
    std::filesystem::remove(kTmpDataFile);
    ::cscrypto::Hash result;
    std::copy(
      buf.begin(),
      std::next(buf.begin(), ::cscrypto::kHashSize),
      result.begin()
    );
    return result;
}

void WalletsCache_Serializer::load(const std::filesystem::path& rootDir) {
    std::ifstream ifs(rootDir / "walletscache.dat");
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
