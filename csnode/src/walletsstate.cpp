#include <csnode/walletsstate.hpp>

#include <csnode/blockchain.hpp>

namespace cs {

WalletsState::WalletsState(const BlockChain& bc) : cacheSptr_(bc.getCacheUpdater()) {}

void WalletsState::updateFromSource() {
    storage_.clear();
}

WalletsState::WalletData& WalletsState::getData(const WalletAddress& address) {
    auto pubKey = cacheSptr_->toPublicKey(address);
    auto it = storage_.find(pubKey);
    if (it != storage_.end()) {
        return it->second;
    }
    else {
        auto walletPtr = cacheSptr_->findWallet(address);
        if (walletPtr) {
            auto res = storage_.insert(std::make_pair(pubKey,
                                                      WalletData{noInd_,
                                                                 walletPtr->balance_,
                                                                 walletPtr->trxTail_}));
            return res.first->second;
        }
        else {
            auto res = storage_.insert(std::make_pair(pubKey, WalletData{noInd_}));
            return res.first->second;
        }
    }
}
}  // namespace cs
