#ifndef BLOCKTAKEOVER_HPP
#define BLOCKTAKEOVER_HPP

#include <lib/system/common.hpp>
#include <lib/system/utils.hpp>

namespace cs {

// A round can end with two valid blocks of one sequence that differ only in their real trusted set: a
// trusted node that finished stage 3 with every signature keeps its block, while the others retried
// stage 3 without a late node and the writer published that block (a stage-3 retry race). Nothing in
// the official node reconciles them: the node keeps its own block for good and no longer matches the
// next rounds' hashes. What a node does with the round package's block when it holds its own:
enum class LastBlockVerdict {
    Same,                 // the same real trusted set: the same block
    MoreSignatures,       // the package's block has more real trusted: the official rule takes it
    Take,                 // fewer, but a majority of all trusted of the round, and the other set cannot form a round
    KeepSizeMismatch,     // the masks are not of the same round
    KeepNoMajority,       // the package's signers are not a majority of the round's trusted
    KeepOtherSideMayWin,  // the nodes outside the package's set could still form a round on their own block
};

// real trusted in a mask (255 marks a trusted that did not sign)
inline size_t realTrustedCount(const cs::Bytes& mask) {
    size_t n = 0;
    for (auto b : mask) {
        n += b != 255 ? 1 : 0;
    }
    return n;
}

// own and package are real trusted masks of the same round (one byte per trusted, 255 = not trusted).
// A majority of all trusted of the round must have signed the package's block, so a minority of
// compromised signers can never make a node take a block; and the nodes outside the package's set
// (who may hold the other block) must be too few to form a round (minTrusted) on their own, so a node
// never leaves a side that could still win
inline LastBlockVerdict lastBlockVerdict(const cs::Bytes& own, const cs::Bytes& package, size_t minTrusted) {
    if (own.empty() || own.size() != package.size()) {
        return LastBlockVerdict::KeepSizeMismatch;
    }
    if (Utils::maskToBits(own) == Utils::maskToBits(package)) {
        return LastBlockVerdict::Same;
    }
    const size_t n = own.size();
    const size_t w = realTrustedCount(package);
    const size_t o = realTrustedCount(own);
    if (w > o) {
        return LastBlockVerdict::MoreSignatures;
    }
    if (w * 2 <= n) {
        return LastBlockVerdict::KeepNoMajority;
    }
    if (n - w >= minTrusted) {
        return LastBlockVerdict::KeepOtherSideMayWin;
    }
    return LastBlockVerdict::Take;
}

inline bool packageBlockWins(LastBlockVerdict v) {
    return v == LastBlockVerdict::MoreSignatures || v == LastBlockVerdict::Take;
}

inline const char* toString(LastBlockVerdict v) {
    switch (v) {
        case LastBlockVerdict::Same: return "same block";
        case LastBlockVerdict::MoreSignatures: return "more real trusted";
        case LastBlockVerdict::Take: return "a majority of the round, the other set cannot form a round";
        case LastBlockVerdict::KeepSizeMismatch: return "masks of different rounds";
        case LastBlockVerdict::KeepNoMajority: return "not signed by a majority of the round's trusted";
        case LastBlockVerdict::KeepOtherSideMayWin: return "the other set could still form a round";
    }
    return "?";
}

}  // namespace cs

#endif  // BLOCKTAKEOVER_HPP
