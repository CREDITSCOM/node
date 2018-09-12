#include <csnode/WalletsIds.h>

namespace Credits {

bool WalletsIds::insert(const WalletAddress& address, WalletId id)
{
    if (!address.is_public_key())
        return false;
    std::pair<Data::const_iterator, bool> res = data_.insert(std::make_pair(address, id));
    if (id >= nextId_)
        nextId_ = id + 1;
    return res.second;
}

bool WalletsIds::find(const WalletAddress& address, WalletId& id) const
{
    if (!address.is_public_key())
        return false;
    Data::const_iterator it = data_.find(address);
    if (it == data_.end())
        return false;
    id = it->second;
    return true;
}

bool WalletsIds::get(const WalletAddress& address, WalletId& id)
{
    if (!address.is_public_key())
        return false;
    std::pair<Data::const_iterator, bool> res = data_.insert(std::make_pair(address, nextId_));
    if (res.second)
        ++nextId_;
    id = res.first->second;
    return res.second;
}

}