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
      realTrustedString = realTrustedString + "[" + std::to_string(static_cast<int>(i)) + "] ";
    }
    csdebug() << "     SENDER = " << static_cast<int>(sender) << ", WRITER = " << static_cast<int>(writer) << ", RealTrusted = " << realTrustedString;
    csdebug() << "     BlockHash = " << cs::Utils::byteStreamToHex(blockHash);
    csdebug() << "     BlockSign = " << cs::Utils::byteStreamToHex(blockSignature);
    csdebug() << "     RoundHash = " << cs::Utils::byteStreamToHex(roundHash);
    csdebug() << "     RoundSign = " << cs::Utils::byteStreamToHex(roundSignature);
    csdebug() << "     TrustHash = " << cs::Utils::byteStreamToHex(trustedHash);
    csdebug() << "     TrustSign = " << cs::Utils::byteStreamToHex(trustedSignature);
  }

  uint8_t sender;
  uint8_t writer;
  std::vector<uint8_t> realTrustedMask;
  Signature blockSignature;
  Hash blockHash;
  Signature roundSignature;
  Hash roundHash;
  Hash trustedHash;
  Signature trustedSignature;
  Signature signature;

};

//smart-contracts stages
struct StageOneSmarts {
  uint8_t sender;
  cs::Sequence sRoundNum;
  cs::PublicKey smartAddress;
  csdb::Amount fee;
  Hash hash;
  Hash messageHash;
  Signature signature;
  Bytes message;
};

struct StageTwoSmarts {
  uint8_t sender;
  cs::Sequence sRoundNum;
  cs::PublicKey smartAddress;
  std::vector<Hash> hashes;  // hashes of stage one
  std::vector<Signature> signatures;
  Signature signature;
  Bytes message;
};

struct StageThreeSmarts {
  uint8_t sender;
  cs::Sequence sRoundNum;
  cs::PublicKey smartAddress;
  uint8_t writer;
  std::vector<uint8_t> realTrustedMask;
  Signature packageSignature;
  Signature signature;
  Bytes message;
};

struct Stage{
  uint8_t msgType;
  std::string msgData;
  cs::RoundNumber msgRoundNum;
  cs::PublicKey msgSender;
};

}  // namespace cs

#endif // STAGE_HPP
