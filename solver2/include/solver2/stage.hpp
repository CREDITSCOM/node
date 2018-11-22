#pragma once

#include <consensus.hpp>
#include <cstdint>
#include <lib/system/keys.hpp>

namespace cs {
struct StageOne {
  uint8_t sender;
  Hash hash;
  uint8_t candidatesAmount;
  PublicKey candiates[Consensus::MaxTrustedNodes];
  Signature sig;
};
struct StageTwo {
  uint8_t sender;
  uint8_t trustedAmount;
  Signature signatures[Consensus::MaxTrustedNodes];
  Signature sig;
};
struct StageThree {
  uint8_t sender;
  uint8_t writer;
  Hash hashBlock;
  Hash hashCandidatesList;
  Signature sig;
};

}  // namespace cs
