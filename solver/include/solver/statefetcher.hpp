#ifndef STATEFETCHER_HPP
#define STATEFETCHER_HPP

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include <csdb/address.hpp>
#include <lib/system/common.hpp>

namespace cs {

// The contracts whose state this node holds is not the one the chain declares, and when to ask the
// confidants for it again: at most kRequestsPerRound requests a round, oldest first, each contract
// waiting min(kBaseBackoff * 2^attempts, kMaxBackoff) rounds after a request
// (docs/specs/2026-10-24-contract-state-sync-design.md, 3.4). It only keeps the bookkeeping: the
// caller sends the requests and verifies the replies.
class StateFetcher {
public:
    static constexpr size_t kRequestsPerRound = 16;
    static constexpr cs::RoundNumber kBaseBackoff = 50;
    static constexpr cs::RoundNumber kMaxBackoff = 5000;

    void markStale(const csdb::Address& contract) {
        entries_.try_emplace(contract);
    }

    // the state arrived and verified, or the contract is no longer stale
    void done(const csdb::Address& contract) {
        entries_.erase(contract);
    }

    // a reply about this contract is an answer to this node
    bool isWanted(const csdb::Address& contract) const {
        return entries_.count(contract) != 0;
    }

    // the contracts to ask about in this round; each one counts as asked
    std::vector<csdb::Address> due(cs::RoundNumber round, size_t limit = kRequestsPerRound) {
        std::vector<std::pair<cs::RoundNumber, csdb::Address>> ready;
        for (const auto& [contract, e] : entries_) {
            if (!e.asked || round >= e.lastRequest + backoff(e.attempts)) {
                ready.emplace_back(e.asked ? e.lastRequest : 0, contract);
            }
        }
        std::sort(ready.begin(), ready.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<csdb::Address> out;
        for (size_t i = 0; i < ready.size() && out.size() < limit; ++i) {
            auto& e = entries_[ready[i].second];
            if (e.asked) {
                ++e.attempts;
            }
            e.asked = true;
            e.lastRequest = round;
            out.push_back(ready[i].second);
        }
        return out;
    }

    size_t size() const {
        return entries_.size();
    }

    void reset() {
        entries_.clear();
    }

    static cs::RoundNumber backoff(uint32_t attempts) {
        cs::RoundNumber b = kBaseBackoff;
        for (uint32_t i = 0; i < attempts && b < kMaxBackoff; ++i) {
            b *= 2;
        }
        return std::min(b, kMaxBackoff);
    }

private:
    struct Entry {
        bool asked = false;
        cs::RoundNumber lastRequest = 0;
        uint32_t attempts = 0;
    };
    std::map<csdb::Address, Entry> entries_;
};

}  // namespace cs

#endif  // STATEFETCHER_HPP
