#pragma once

#include <consensus.hpp>
#include <cstdint>
#include <lib/system/keys.hpp>
#include <csnode/transactionspacket.hpp>

namespace cs {
  struct StageOne
  {
    uint8_t sender;
    Hash hash;
    std::string roundTimeStamp;
    std::vector <PublicKey> trustedCandidates;
    std::vector <TransactionsPacketHash> hashesCandidates;
    Hash msgHash;
    Signature sig;
  };
  struct StageTwo
  {
    uint8_t sender;
    std::vector <Hash> hashes; //hashes of the previous message
    std::vector <Signature> signatures;
    Signature sig;
  };
  struct StageThree
  {
    uint8_t sender;
    uint8_t writer;
    std::vector <uint8_t> realTrustedMask;
    Hash hashBlock;
    Hash hashHashesList;
    Hash hashCandidatesList;
    Signature sig;
  };

}  // namespace cs
