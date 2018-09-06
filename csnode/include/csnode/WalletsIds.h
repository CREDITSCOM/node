#ifndef WALLET_IDS_H
#define WALLET_IDS_H

#include <unordered_map>
#include <csdb/address.h>
#include "csdb/internal/types.h"

namespace Credits
{

class WalletsIds
{
public:
    using WalletId = csdb::internal::WalletId;
    using WalletAddress = csdb::Address;
public:
    WalletsIds() : nextId_(0)
    {}
    bool insert(const WalletAddress& address, WalletId id);
    bool find(const WalletAddress& address, WalletId& id) const;
    bool get(const WalletAddress& address, WalletId& id);
private:
    using Data = std::unordered_map<WalletAddress, WalletId>;
    Data data_;
    WalletId nextId_;
};

}

#endif