#ifndef CONFIRMATIONLIST_HPP
#define CONFIRMATIONLIST_HPP

#include <map>
#include <optional>

#include <csnode/nodecore.hpp>

#include <lib/system/common.hpp>

namespace cs {
struct TrustedConfirmation {
    bool bigBang = false;
    cs::ConfidantsKeys confidants;
    cs::Bytes mask;
    cs::Signatures signatures;
};

class ConfirmationList {
public:
    void add(cs::RoundNumber rNum, bool bang, const cs::ConfidantsKeys& confidants, const cs::Bytes& confirmationsMask, const cs::Signatures& confirmation);
    void remove(cs::RoundNumber);
    std::optional<cs::TrustedConfirmation> find(cs::RoundNumber) const;

private:
    // confidant confirmation
    std::map<cs::RoundNumber, TrustedConfirmation> confirmationList_;
};
}  // namespace cs

#endif  // CONFIRMATIONLIST_HPP
