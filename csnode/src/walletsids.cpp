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
        std::pair<Data::const_iterator, bool> res = norm_.data_.insert(std::make_pair(address, id));
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
        Data::const_iterator it = norm_.data_.find(address);
        if (it == norm_.data_.end())
            return false;
        id = it->second;
        return true;
    }
    cserror() << "Wrong address";
    return false;
}

bool WalletsIds::Normal::findaddr(const WalletId& id, WalletAddress& address) const {
    bool flgfind = false;
    for (auto& it : norm_.data_) {
        if (it.second == id) {
            flgfind = true;
            address = it.first;
            break;
        }
    }
    if (flgfind)
        return true;

    cserror() << "Wrong WalletId";
    return false;
}

bool WalletsIds::Normal::get(const WalletAddress& address, WalletId& id) {
    if (address.is_wallet_id()) {
        id = address.wallet_id();
        return false;
    }
    else if (address.is_public_key()) {
        std::pair<Data::const_iterator, bool> res = norm_.data_.insert(std::make_pair(address, norm_.nextId_));
        if (res.second) {
            if (norm_.nextId_ >= numeric_limits<WalletId>::max() / 2)
                throw runtime_error("nextId_ >= numeric_limits<WalletId>::max() / 2");
            ++norm_.nextId_;
        }
        id = res.first->second;
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
    csdebug() << "Keys before erasing address " << address.to_string();
    for (auto& it : norm_.data_) {
        csdebug() << it.second << " - " << it.first.to_string();
    }
    norm_.data_.erase(address);
    if (norm_.nextId_ > 0) {
        --norm_.nextId_;
    }
    csdebug() << "Keys after erasing";
    for (auto& it : norm_.data_) {
        csdebug() << it.second << " - " << it.first.to_string();
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
        std::pair<Data::iterator, bool> res = norm_.data_.insert(std::make_pair(address, idNormal));

        const bool isInserted = res.second;
        auto& value = res.first->second;

        if (!isInserted) {
            if (!isSpecial(value))
                return false;
            idSpecial = value;
            value = idNormal;
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
        std::pair<Data::const_iterator, bool> res = norm_.data_.insert(std::make_pair(address, nextIdSpecial_));
        if (res.second) {
            if (nextIdSpecial_ == numeric_limits<WalletId>::max())
                throw runtime_error("nextIdSpecial_ == numeric_limits<WalletId>::max()");
            ++nextIdSpecial_;
        }
        id = res.first->second;
        return true;
    }
    cserror() << "Wrong address";
    return false;
}

}  // namespace cs
