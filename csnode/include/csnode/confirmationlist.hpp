#pragma once
#include <csnode/nodecore.hpp>
#include <lib/system/common.hpp>

#include <map>
#include <optional>

namespace cs
{

  struct TrustedConfirmation {
    bool bigBang = false;
    cs::ConfidantsKeys confidants;
    cs::Bytes mask;
    cs::Signatures signatures;
  };

  class ConfirmationList
  {
  public:
    void add(cs::RoundNumber rNum, bool bang, cs::ConfidantsKeys confidants, cs::Bytes confirmationsMask, cs::Signatures confirmation);
    void remove(cs::RoundNumber);
    std::optional<cs::TrustedConfirmation> find(cs::RoundNumber) const;


  private:
    //confidant confirmation
    std::map<cs::RoundNumber, TrustedConfirmation> confirmationList_;

  };

} // cs
