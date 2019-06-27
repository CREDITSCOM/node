#include <csnode/confirmationlist.hpp>

namespace cs {
void ConfirmationList::add(cs::RoundNumber rNum, bool bang, const cs::ConfidantsKeys& confidants, const cs::Bytes& confirmationsMask, const cs::Signatures& confirmation) {
    if (confirmationList_.find(rNum) != confirmationList_.cend()) {
        remove(rNum);
    }

    TrustedConfirmation tConfirmation;
    tConfirmation.bigBang = bang;
    tConfirmation.confidants = confidants;
    tConfirmation.mask = confirmationsMask;
    tConfirmation.signatures = confirmation;

    confirmationList_.emplace(rNum, std::move(tConfirmation));
    csdebug() << "The confirmation of R-" << rNum << " added, conf.size = " << confirmationList_.size();
    if (rNum > 5) {
        for (auto& it : confirmationList_) {
            if (it.first < rNum - 5) {
                confirmationList_.erase(it.first);
            }
        }
        csdebug() << "Some confirmations were deleted, conf.size = " << confirmationList_.size();
    }

}

void ConfirmationList::remove(cs::RoundNumber rNum) {
    if (confirmationList_.find(rNum) != confirmationList_.end()) {
        confirmationList_.erase(rNum);
        csdebug() << "The confirmation of R-" << rNum << " was successfully erased, conf.size = " << confirmationList_.size();
    }
    else {
        csdebug() << "The confirmation of R-" << rNum << " was not found";
    }
}

std::optional<cs::TrustedConfirmation> ConfirmationList::find(cs::RoundNumber rNum) const {
    const auto it = confirmationList_.find(rNum);

    if (it == confirmationList_.end()) {
        csdebug() << "The confirmation of R-" << rNum << " was not found, conf.size = " << confirmationList_.size();
        return std::nullopt;
    }
    csdebug() << "The confirmation of R - " << rNum << " was found, conf.size = " << confirmationList_.size();
    return std::make_optional<cs::TrustedConfirmation>(it->second);
}
}  // namespace cs
