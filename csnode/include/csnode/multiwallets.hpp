#ifndef MULTIWALLETS_HPP
#define MULTIWALLETS_HPP

#include <mutex>

#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

#include <csnode/walletscache.hpp>

using namespace boost::multi_index;

namespace cs {
class MultiWallets {
public:
    struct InternalData {
        PublicKey key;
        csdb::Amount balance;
        uint64_t transactionsCount;
#ifdef MONITOR_NODE
        uint64_t createTime;
#endif
    };

    enum Tags {
        ByPublicKey,
        ByBalance,
        ByTransactionsCount
#ifdef MONITOR_NODE
       , ByCreateTime
#endif
    };

    enum class Order {
        Less,
        Greater
    };

    bool contains(const PublicKey& key) const;
    size_t size() const;

    csdb::Amount balance(const PublicKey& key) const;
    uint64_t transactionsCount(const PublicKey& key) const;

#ifdef MONITOR_NODE
    uint64_t createTime(const PublicKey& key) const;
#endif

    template<Tags tag>
    std::vector<InternalData> iterate(int64_t offset, int64_t limit, Order order = Order::Greater) const {
        cs::Lock lock(mutex_);
        auto& bucket = indexes_.get<tag>();

        if (static_cast<int64_t>(bucket.size()) < offset || limit == 0) {
            return {};
        }

        auto capacity = std::min(static_cast<int64_t>(bucket.size()) - offset, limit);
        return (order == Order::Greater) ? shadowIterate<Order::Greater>(bucket, offset, limit, capacity) :
                                           shadowIterate<Order::Less>(bucket, offset, limit, capacity);
    }

public slots:
    void onDbReadFinished(const std::unordered_map<PublicKey, WalletsCache::WalletData>& data);
    void onWalletCacheUpdated(const PublicKey& key, const WalletsCache::WalletData& data);

protected:
    InternalData map(const PublicKey& key, const WalletsCache::WalletData& data);

    template <Order order, typename Bucket>
    std::vector<InternalData> shadowIterate(Bucket& bucket, int64_t offset, int64_t limit, int64_t capacity) const {
        std::vector<InternalData> result;
        result.reserve(static_cast<size_t>(capacity));

        auto [begin, end] = rangeOrder<order>(bucket);
        std::advance(begin, offset);

        for (; begin != end; ++begin) {
            result.push_back(*begin);

            if (result.size() == static_cast<size_t>(limit)) {
                break;
            }
        }

        return result;
    }

    template<Order order, typename Bucket>
    constexpr auto rangeOrder(Bucket& bucket) const {
        if constexpr(order == Order::Greater) {
            return std::make_pair(bucket.begin(), bucket.end());
        }
        else {
            return std::make_pair(bucket.rbegin(), bucket.rend());
        }
    }

private:
    using Container = boost::multi_index_container<InternalData,
                        indexed_by<
                            hashed_unique<member<InternalData, PublicKey, &InternalData::key>>,
                            ordered_non_unique<member<InternalData, csdb::Amount, &InternalData::balance>, std::greater<csdb::Amount>>,
                            ordered_non_unique<member<InternalData, uint64_t, &InternalData::transactionsCount>, std::greater<uint64_t>>
#ifdef MONITOR_NODE
                            ,
                            ordered_non_unique<member<InternalData, uint64_t, &InternalData::createTime>, std::greater<uint64_t>>
#endif
                        >
                      >;
    mutable std::mutex mutex_;
    Container indexes_;
};
}

#endif  // MULTIWALLETS_HPP
