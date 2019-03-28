#include <csnode/confirmationlist.hpp>

namespace cs
{

  void ConfirmationList::add(cs::RoundNumber rNum, bool bang, cs::ConfidantsKeys confidants, cs::Bytes confirmationsMask, cs::Signatures confirmation) {
    if (confirmationList_.find(rNum) != confirmationList_.cend()) {
      remove(rNum);
    }
    TrustedConfirmation tConfirmation;
    tConfirmation.bigBang = bang;
    tConfirmation.confidants = confidants;
    tConfirmation.mask = confirmationsMask;
    tConfirmation.signatures = confirmation;
    confirmationList_.emplace(rNum, tConfirmation);
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

} // cs
