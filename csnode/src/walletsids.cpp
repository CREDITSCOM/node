#include <csnode/walletsids.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <limits>

using namespace std;

namespace cs {
WalletsIds::WalletsIds()
: nextId_(0) {
    norm_.reset(new Normal(*this));
}

WalletsIds::Normal::Normal(WalletsIds& norm)
: norm_(norm) {
}

bool WalletsIds::Normal::insert(const WalletAddress& address, WalletId id) {
    if (address.is_wallet_id()) {
        if (id != address.wallet_id()) {
            cserror() << "Wrong address";
        }
        return false;
    }
    else if (address.is_public_key()) {
        auto res = norm_.data_.insert({address, id});
        if (res.second && id >= norm_.nextId_) {
            if (id >= numeric_limits<WalletId>::max() / 2)
                throw runtime_error("idNormal >= numeric_limits<WalletId>::max() / 2");

            norm_.nextId_ = id + 1;
        }
        return res.second;
    }
    cserror() << "Wrong address";
    return false;
}

bool WalletsIds::Normal::find(const WalletAddress& address, WalletId& id) const {
    if (address.is_wallet_id()) {
        id = address.wallet_id();
        return true;
    }
    else if (address.is_public_key()) {
        const auto &index = norm_.data_.get<Wallet::byAddress>();
        auto it = index.find(address);
        if (it == index.end()) {
            return false;
        }
        id = (*it).id;
        return true;
    }
    cserror() << "Wrong address";
    return false;
}

bool WalletsIds::Normal::findaddr(const WalletId& id, WalletAddress& address) const {
    const auto &index = norm_.data_.get<Wallet::byId>();
    auto it = index.find(id);
    if (it == index.end()) {
        cserror() << "Wrong WalletId";
        return false;
    }
    address = (*it).address;
    return true;
}

bool WalletsIds::Normal::get(const WalletAddress& address, WalletId& id) {
    if (address.is_wallet_id()) {
        id = address.wallet_id();
        return false;
    }
    else if (address.is_public_key()) {
        auto res = norm_.data_.insert({address, norm_.nextId_});
        if (res.second) {
            if (norm_.nextId_ >= numeric_limits<WalletId>::max() / 2)
                throw runtime_error("nextId_ >= numeric_limits<WalletId>::max() / 2");
            ++norm_.nextId_;
        }
        id = (*res.first).id;
        return res.second;
    }
    cserror() << "Wrong address";
    return false;
}

void WalletsIds::Normal::fillIds(std::map<csdb::Address, std::pair<WalletId, csdb::Pool::NewWalletInfo::AddressId>>& addrsAndIds) {
    auto nextId = norm_.nextId_;

    for (auto& addrAndId : addrsAndIds) {
        if (addrAndId.first.is_wallet_id()) {
            addrAndId.second.first = addrAndId.first.wallet_id();
            continue;
        }

        if (addrAndId.first.is_public_key()) {
            auto it = norm_.data_.find(addrAndId.first);
            if (it != norm_.data_.end()) {
                addrAndId.second.first = kWrongWalletId;
            }
            else {
                addrAndId.second.first = nextId++;
                if (norm_.nextId_ >= numeric_limits<WalletId>::max() / 2) {
                    throw runtime_error("nextId_ >= numeric_limits<WalletId>::max() / 2");
                }
            }
        }
    }
}

bool WalletsIds::Normal::remove(const WalletAddress& address) {
    if (address.is_wallet_id()) {
        cserror() << __func__ << ": wrong address type";
        return false;
    }

    const auto &index = norm_.data_.get<Wallet::byAddress>();
    auto it = index.find(address);
    if (it != index.end()) {
        norm_.data_.erase(it);
        if (norm_.nextId_ > 0) {
            --norm_.nextId_;
        }
    }
    return true;
}
}  // namespace cs
