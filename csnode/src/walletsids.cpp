#include <csnode/walletsids.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <limits>

using namespace std;

namespace cs {

WalletsIds::WalletsIds()
: nextId_(0) {
    special_.reset(new Special(*this));
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

WalletsIds::WalletId WalletsIds::Special::makeSpecial(WalletId id) {
    return (id | maskSpecial_);
}

WalletsIds::WalletId WalletsIds::Special::makeNormal(WalletId id) {
    return (id & ~maskSpecial_);
}

bool WalletsIds::Special::isSpecial(WalletId id) {
    return (id & maskSpecial_) != 0;
}

WalletsIds::Special::Special(WalletsIds& norm)
: norm_(norm)
, nextIdSpecial_(makeSpecial(0)) {
}

bool WalletsIds::Special::insertNormal(const WalletAddress& address, WalletId idNormal, WalletId& idSpecial) {
    idSpecial = noSpecial_;

    if (address.is_wallet_id()) {
        if (idNormal != address.wallet_id()) {
            cserror() << "Wrong address";
        }
        return false;
    }
    else if (address.is_public_key()) {
        auto res = norm_.data_.insert({address, idNormal});

        const bool isInserted = res.second;
        auto& value = (*res.first).id;

        if (!isInserted) {
            if (!isSpecial(value)) {
                return false;
            }
            idSpecial = value;
            auto &index = norm_.data_.get<Wallet::byId>();
            auto it = norm_.data_.project<Wallet::byId>(res.first);
            index.modify(it, Wallet::idChange(idNormal));
        }

        if (idNormal >= norm_.nextId_) {
            if (idNormal >= numeric_limits<WalletId>::max() / 2)
                throw runtime_error("idNormal >= numeric_limits<WalletId>::max() / 2");

            norm_.nextId_ = idNormal + 1;
        }
        return true;
    }
    cserror() << "Wrong address";
    return false;
}

bool WalletsIds::Special::findAnyOrInsertSpecial(const WalletAddress& address, WalletId& id) {
    if (address.is_wallet_id()) {
        id = address.wallet_id();
        return true;
    }
    else if (address.is_public_key()) {
        auto res = norm_.data_.insert({address, nextIdSpecial_});
        if (res.second) {
            if (nextIdSpecial_ == numeric_limits<WalletId>::max())
                throw runtime_error("nextIdSpecial_ == numeric_limits<WalletId>::max()");
            ++nextIdSpecial_;
        }
        id = (*res.first).id;
        return true;
    }
    cserror() << "Wrong address";
    return false;
}

}  // namespace cs
