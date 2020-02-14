#ifndef WALLET_IDS_HPP
#define WALLET_IDS_HPP

#include <map>
#include <memory>
#include <type_traits>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>

#include <csdb/address.hpp>
#include <csdb/internal/types.hpp>
#include <csdb/pool.hpp>

using namespace boost::multi_index;

namespace cs {

class WalletsIds {
public:
    using WalletId = csdb::internal::WalletId;
    using WalletAddress = csdb::Address;
    constexpr static WalletId kWrongWalletId = std::numeric_limits<WalletId>::max();

public:
    class Normal {
    public:
        Normal(WalletsIds& norm);

        // returns true if new id was inserted
        bool insert(const WalletAddress& address, WalletId id);
        bool find(const WalletAddress& address, WalletId& id) const;
        bool findaddr(const WalletId& id, WalletAddress& address) const;
        // returns true if new id was inserted
        bool get(const WalletAddress& address, WalletId& id);
        bool remove(const WalletAddress& address);
        void fillIds(std::map<csdb::Address, std::pair<WalletId, csdb::Pool::NewWalletInfo::AddressId>>& addrsAndIds);

    private:
        WalletsIds& norm_;
    };

public:
    WalletsIds();
    WalletsIds(const WalletsIds&) = delete;
    WalletsIds& operator=(const WalletsIds&) = delete;
    WalletsIds(const WalletsIds&&) = delete;
    WalletsIds& operator=(const WalletsIds&&) = delete;
    
    Normal& normal() {
        return *norm_;
    }
    const Normal& normal() const {
        return *norm_;
    }

private:
    struct Wallet {
        WalletAddress address; struct byAddress {};
        WalletId id;  struct byId {};
    };

    using Data = multi_index_container<
        Wallet,
        indexed_by<
            hashed_unique<
                tag<Wallet::byAddress>, member<
                    Wallet, WalletAddress, &Wallet::address
                >
            >,
            hashed_unique<
                tag<Wallet::byId>, member<
                    Wallet, WalletId, &Wallet::id
                >
            >
        >
    >;

    Data data_;
    WalletId nextId_;
    std::unique_ptr<Normal> norm_;
};

}  // namespace cs

#endif
