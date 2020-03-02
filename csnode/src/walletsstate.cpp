#include <csnode/walletsstate.hpp>

namespace cs {

WalletsState::WalletData& WalletsState::getData(const WalletAddress& address) {
    auto pubKey = wallCache_.toPublicKey(address);
    auto it = storage_.find(pubKey);
    if (it != storage_.end()) {
        return it->second;
    }
    else {
        auto walletPtr = wallCache_.findWallet(address);
        if (walletPtr) {
            auto res = storage_.insert(std::make_pair(pubKey,
                                                      WalletData{noInd_,
                                                                 walletPtr->balance_,
                                                                 walletPtr->delegated_,
                                                                 walletPtr->delegateSources_ ? *walletPtr->delegateSources_ : std::map<cs::PublicKey, std::vector<cs::TimeMoney>>{},// accounts, delegated to current account
                                                                 walletPtr->delegateTargets_ ? *walletPtr->delegateTargets_ : std::map<cs::PublicKey, std::vector<cs::TimeMoney>>{},// accounts to which current round delegated special amounts 
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
