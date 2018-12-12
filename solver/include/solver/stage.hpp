#ifndef STAGE_HPP
#define STAGE_HPP

#include <consensus.hpp>
#include <csnode/nodecore.hpp>
#include <cstdint>
#include <lib/system/keys.hpp>

namespace cs {
struct StageOne {
  uint8_t sender;
  Hash hash;
  std::string roundTimeStamp;
  std::vector<PublicKey> trustedCandidates;
  std::vector<TransactionsPacketHash> hashesCandidates;
  Hash messageHash;
  Signature signature;
};

struct StageTwo {
  uint8_t sender;
  cs::Hashes hashes;  // hashes of stage one
  cs::Signatures signatures;
  Signature signature;
};

struct StageThree {
  uint8_t sender;
  uint8_t writer;
  std::vector<uint8_t> realTrustedMask;
  Hash hashBlock;
  Hash hashHashesList;
  Hash hashCandidatesList;
  Signature signature;
};

//smart-contracts stages
struct StageOneSmarts {
  uint8_t sender;
  csdb::Pool::sequence_t sRoundNum;
  Hash hash;
  Hash messageHash;
  Signature signature;
};

struct StageTwoSmarts {
  uint8_t sender;
  csdb::Pool::sequence_t sRoundNum;
  std::vector<Hash> hashes;  // hashes of stage one
  std::vector<Signature> signatures;
  Signature signature;
};

struct StageThreeSmarts {
  uint8_t sender;
  csdb::Pool::sequence_t sRoundNum;
  uint8_t writer;
  std::vector<uint8_t> realTrustedMask;
  Hash finalHash;
  Signature signature;
};

}  // namespace cs

#endif // STAGE_HPP
