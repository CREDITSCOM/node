#ifndef STAGE_HPP
#define STAGE_HPP

#include <consensus.hpp>
#include <csnode/transactionspacket.hpp>
#include <cstdint>
#include <lib/system/keys.hpp>

namespace cs {
struct StageOne {
  uint8_t sender;
  Hash hash;
  std::string roundTimeStamp;
  std::vector<PublicKey> trustedCandidates;
  std::vector<TransactionsPacketHash> hashesCandidates;
  Hash msgHash;
  Signature sig;
};

struct StageTwo {
  uint8_t sender;
  std::vector<Hash> hashes;  // hashes of stage one
  std::vector<Signature> signatures;
  Signature sig;
};

struct StageThree {
  uint8_t sender;
  uint8_t writer;
  std::vector<uint8_t> realTrustedMask;
  Hash hashBlock;
  Hash hashHashesList;
  Hash hashCandidatesList;
  Signature sig;
};

//smart-contracts stages
struct StageOneSmarts {
  uint8_t sender;
  Hash hash;
  Hash msgHash;
  Signature sig;
};

struct StageTwoSmarts {
  uint8_t sender;
  std::vector<Hash> hashes;  // hashes of stage one
  std::vector<Signature> signatures;
  Signature sig;
};

struct StageThreeSmarts {
  uint8_t sender;
  uint8_t writer;
  std::vector<uint8_t> realTrustedMask;
  Hash finalHash;
  Signature sig;
};

}  // namespace cs

#endif STAGE_HPP
