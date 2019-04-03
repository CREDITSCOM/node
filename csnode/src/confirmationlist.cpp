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
}

void ConfirmationList::remove(cs::RoundNumber rNum) {
  if (confirmationList_.find(rNum) != confirmationList_.end()) {
    confirmationList_.erase(rNum);
    csdebug() << "The confirmation of R-" << rNum << " was successfully erased";
  }
  else {
    csdebug() << "The confirmation of R-" << rNum << " was not found";
  }
}

std::optional<cs::TrustedConfirmation> ConfirmationList::find(cs::RoundNumber rNum) const {
  const auto it = confirmationList_.find(rNum);
  if (it == confirmationList_.end()) {
    return std::nullopt;
  }
  return std::make_optional<cs::TrustedConfirmation>(it->second);
}
}  // namespace cs
