#ifndef STAGE_HPP
#define STAGE_HPP

#include <consensus.hpp>
#include <csnode/nodecore.hpp>
#include <cstdint>

namespace cs {

  constexpr const uint8_t InvalidSender = uint8_t(-1);

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

//struct StageThree {
//  uint8_t sender;
//  uint8_t writer;
//  std::vector<uint8_t> realTrustedMask;
//  Hash hashBlock;
//  Hash hashHashesList;
//  Hash hashCandidatesList;
//  Signature signature;
//};

struct StageThree {
  void print() {
    std::string realTrustedString;
    for (auto& i : realTrustedMask) {
      realTrustedString = realTrustedString + "[" + std::to_string((int)i) + "] ";
    }
    csdebug() << "     SENDER = " << (int)sender << ", WRITER = " << (int)writer << ", RealTrusted = " << realTrustedString;
    csdebug() << "     BlockHash = " << cs::Utils::byteStreamToHex(blockHash.data(), blockHash.size());
    csdebug() << "     BlockSign = " << cs::Utils::byteStreamToHex(blockSignature.data(), blockSignature.size());
    csdebug() << "     RoundHash = " << cs::Utils::byteStreamToHex(roundHash.data(), roundHash.size());
    csdebug() << "     RoundSign = " << cs::Utils::byteStreamToHex(roundSignature.data(), roundSignature.size());
  }

  uint8_t sender;
  uint8_t writer;
  std::vector<uint8_t> realTrustedMask;
  Signature blockSignature;
  Hash blockHash;
  Signature roundSignature;
  Hash roundHash;
  Signature signature;
};

//smart-contracts stages
struct StageOneSmarts {
  uint8_t sender;
  cs::Sequence sRoundNum;
  Hash hash;
  Hash messageHash;
  Signature signature;
};

struct StageTwoSmarts {
  uint8_t sender;
  cs::Sequence sRoundNum;
  std::vector<Hash> hashes;  // hashes of stage one
  std::vector<Signature> signatures;
  Signature signature;
};

struct StageThreeSmarts {
  uint8_t sender;
  cs::Sequence sRoundNum;
  uint8_t writer;
  std::vector<uint8_t> realTrustedMask;
  Signature packageSignature;
  Signature signature;
};

struct Stage{
  uint8_t msgType;
  std::string msgData;
  cs::RoundNumber msgRoundNum;
  cs::PublicKey msgSender;
};

}  // namespace cs

#endif // STAGE_HPP
