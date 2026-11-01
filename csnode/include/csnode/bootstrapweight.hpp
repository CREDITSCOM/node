#ifndef BOOTSTRAPWEIGHT_HPP
#define BOOTSTRAPWEIGHT_HPP

#include <cstdint>
#include <limits>
#include <set>

#include <csnode/nodecore.hpp>

namespace cs {

// The weight a node gives a bootstrap round table: it takes a table only if it weighs more than the
// one it took last. A table signed with the blockchain key beats every unsigned one.
using BootstrapWeight = uint32_t;
constexpr BootstrapWeight kSignedBootstrapWeight = std::numeric_limits<BootstrapWeight>::max();
// the largest table the official node weighs (in a uint8_t); it drops larger ones
constexpr size_t kOfficialBootstrapTableLimit = 8;
constexpr size_t kMaxBootstrapTable = 25;  // Consensus::MaxTrustedNodes

// Up to 8 keys, exactly the official node's value on x86-64, so that both pick the same table when a
// stalled network bootstraps again: the table is matched against the initial confidants in their
// (sorted) order, and the confidant with index k adds 2^(n-1-k) for a table of n keys. In the official
// code an index k >= n shifts by a wrapped size_t, which x86 masks to 31..8 and the uint8_t sum then
// truncates to 0: it adds nothing, as here. From 9 to 25 keys, where the official node returns 0 and
// drops the table, 2^n on top, so every larger table beats every smaller one, but only for a table
// the starter builds: initial confidants in their order, each once. Any other table of 9 or more keys
// (repeated or unordered keys, which an unsigned table could otherwise use to outweigh an honest one)
// weighs 0 and is dropped, as by the official node.
inline BootstrapWeight bootstrapWeight(const std::set<PublicKey>& initial, const PublicKeys& table) {
    const size_t n = table.size();
    if (n == 0 || n > kMaxBootstrapTable) {
        return 0;
    }
    BootstrapWeight weight = n > kOfficialBootstrapTableLimit ? (BootstrapWeight(1) << n) : 0;
    size_t matched = 0;
    size_t index = 0;
    for (const auto& confidant : initial) {
        if (matched < n && table[matched] == confidant) {
            if (index < n) {
                weight += BootstrapWeight(1) << (n - 1 - index);
            }
            ++matched;
        }
        ++index;
    }
    if (n > kOfficialBootstrapTableLimit && matched != n) {
        return 0;
    }
    return weight;
}

}  // namespace cs

#endif  // BOOTSTRAPWEIGHT_HPP
