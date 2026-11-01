#ifndef BOOTSTRAPGATE_HPP
#define BOOTSTRAPGATE_HPP

#include <algorithm>
#include <cstdint>

#include <lib/system/common.hpp>

namespace cs {

// An honest starter (Node::onRoundTimeElapsed, Node::bootstrap, Node::initBootstrapRP) sends
// round = its last block + 1 and holds the highest block among the initial confidants it sees. A
// receiver level with it sees lastSeq + 1; one that holds one block the starter lacks sees lastSeq;
// one that lacks the starter's last block sees lastSeq + 2; one still fetching blocks that learned
// the network round sees currentRound or currentRound + 1. A bootstrap table for any other round is
// not for this node.
constexpr RoundNumber kBootstrapRoundsAhead = 2;

inline bool isBootstrapRoundInWindow(RoundNumber rNum, Sequence lastSeq, RoundNumber currentRound) {
    if (rNum == 0 || rNum < lastSeq) {
        return false;
    }
    const RoundNumber top = std::max<RoundNumber>(lastSeq, currentRound);
    return rNum <= kBootstrapRoundsAhead || rNum - kBootstrapRoundsAhead <= top;
}

// A node takes an unsigned bootstrap table only when its own round has lasted about
// round_elapse_time, the condition that makes an honest initial confidant start a bootstrap. Half
// of it is margin for the starter's tick (2 s), the propagation of the previous round table and a
// round table fetched late. A node younger than round_elapse_time cannot have observed a stall, and
// a node already in a bootstrap round is restarting with the others: both take the table as the
// official node does.
inline uint64_t bootstrapStallThresholdMs(uint64_t roundElapseMs) {
    return roundElapseMs - roundElapseMs / 2;
}

inline bool isBootstrapStallAccepted(uint64_t msSinceRoundChange, uint64_t msSinceStart, uint64_t roundElapseMs,
                                     bool inBootstrapRound) {
    if (inBootstrapRound || msSinceStart < roundElapseMs) {
        return true;
    }
    return msSinceRoundChange >= bootstrapStallThresholdMs(roundElapseMs);
}

}  // namespace cs

#endif  // BOOTSTRAPGATE_HPP
