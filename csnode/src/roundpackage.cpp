#include <roundpackage.hpp>
#include <csnode/datastream.hpp>
#include <consensus.hpp>
#include <csdb/pool.hpp>
#include <stage.hpp>

namespace cs {
  RoundPackage::RoundPackage() {
  }

  cs::Bytes RoundPackage::toBinary() {
    if (binaryRepresentation_.size() == 0) {
      refillToSign();
    }
    if (binaryRepresentation_.size() == messageSize_) {
      if (roundSignatures_.size() > 0 && poolSignatures_.size() > 0 && trustedSignatures_.size() > 0) {
        cs::DataStream stream(binaryRepresentation_);
        stream << roundSignatures_;
        stream << poolSignatures_;
        stream << trustedSignatures_;
      }
    }
    return binaryRepresentation_;
  }

  cs::Byte RoundPackage::subRound() {
    return subRound_;
  }

  bool RoundPackage::fromBinary(const cs::Bytes& bytes, cs::RoundNumber rNum, cs::Byte subRound) {
    csdebug() << "rPackage-binary: " << cs::Utils::byteStreamToHex(bytes.data(), bytes.size());
    cs::DataStream roundStream(bytes.data(), bytes.size());
    cs::ConfidantsKeys confidants;
    roundTable_.round = rNum;
    //subRound_ = subRound;
    roundStream >> roundTable_.confidants;
    if (roundTable_.confidants.empty() || roundTable_.confidants.size() > Consensus::MaxTrustedNodes) {
      csmeta(cserror) << name() << "Illegal confidants number in round table: " << roundTable_.confidants.size();
      return false;
    }
    roundStream >> poolMetaInfo_.realTrustedMask;
    roundStream >> subRound_ >> iteration_;
    if (poolMetaInfo_.realTrustedMask.size() > Consensus::MaxTrustedNodes) {
      csmeta(cserror) << name() << "Illegal trusted mask size: " << poolMetaInfo_.realTrustedMask.size();
      return false;
    }

    roundStream >> roundTable_.hashes;
    if (roundTable_.hashes.size() > Consensus::MaxStageOneHashes * 2) {
      csmeta(cserror) << name() << "Illegal number of hashes: " << roundTable_.hashes.size();
      return false;
    }

    roundStream >> poolMetaInfo_.timestamp;
    if (poolMetaInfo_.timestamp.size() > 20U) { //TODO: change the number with the appropriate constant
      csmeta(cserror) << name() << "Illegal TimeStamp size: " << poolMetaInfo_.timestamp.size();
      return false;
    }
    roundStream >> poolMetaInfo_.characteristic.mask;
    if (poolMetaInfo_.characteristic.mask.size() > 1'000'000U) { //TODO: change the number with the appropriate constant
      csmeta(cserror) << name() << "Illegal Characteristic Mask size: " << poolMetaInfo_.characteristic.mask.size();
      return false;
    }
    //TODO: think what to do if we grt the roundPackage with greater round
    roundStream >> poolMetaInfo_.sequenceNumber;
    if (poolMetaInfo_.sequenceNumber != roundTable_.round - 1) {
      csmeta(cserror) << name() << "Incorrect new Block sequence: " << poolMetaInfo_.sequenceNumber;
      return false;
    }
    roundStream >> poolMetaInfo_.previousHash;
    roundStream >> roundSignatures_;
    size_t tCount = cs::TrustedMask::trustedSize(poolMetaInfo_.realTrustedMask);
    if (tCount != roundSignatures_.size()) {
      csmeta(cserror) << name() << "Illegal Round Signatures size: " << roundSignatures_.size();
      return false;
    }
    roundStream >> poolSignatures_;
    if (tCount != poolSignatures_.size()) {
      csmeta(cserror) << name() << "Illegal Pool Signatures size: " << poolSignatures_.size();
      return false;
    }
    roundStream >> trustedSignatures_;
    if (tCount != trustedSignatures_.size()) {
      csmeta(cserror) << name() << "Illegal Trusted Confirmations size: " << trustedSignatures_.size();
      return false;
    }
    subRound_ = subRound;
    csdebug() << "RoundPackage retrived successfully";
    return true;
  }

  std::string RoundPackage::toString() {
    std::string packageString;
    packageString = "RoundPackage #" + std::to_string(roundTable_.round) + "." + std::to_string(static_cast<int>(subRound_)) + ":" + "\n"
      + "New Confidants(" + std::to_string(roundTable_.confidants.size()) + "):";
    for (auto it : roundTable_.confidants) {
      packageString = packageString + "\n\t" + cs::Utils::byteStreamToHex(it.data(), it.size());
    }
    packageString = packageString + "\n" + "Hashes for New round(" + std::to_string(roundTable_.hashes.size()) + "):";
    for (auto it : roundTable_.hashes) {
      packageString += "\n\t" + it.toString();
    }
    packageString = packageString + "\n" + "PoolMetaInfo:";
    packageString = packageString + "\n\t" + "Previous Hash:  " + poolMetaInfo_.previousHash.to_string();
    packageString = packageString + "\n\t" + "Characteristic: " + cs::Utils::byteStreamToHex(poolMetaInfo_.characteristic.mask.data(), poolMetaInfo_.characteristic.mask.size());
    packageString = packageString + "\n\t" + "Trusted Mask:   " + cs::TrustedMask::toString(poolMetaInfo_.realTrustedMask);
    packageString = packageString + "\n\t" + "Sequence Number: " + std::to_string(poolMetaInfo_.sequenceNumber);
    packageString = packageString + ", TimeStamp: " + poolMetaInfo_.timestamp;
    //packageString = packageString + "\n" + "Smart Signatures:" + poolMetaInfo_.smartSignatures;
    packageString = packageString + "\n" + "PoolSignatures(" + std::to_string(poolSignatures_.size()) + "):";
    for (auto it : poolSignatures_) {
      packageString = packageString + "\n\t" + cs::Utils::byteStreamToHex(it.data(), it.size());
    }
    packageString = packageString + "\n" + "TrustedSignatures(" + std::to_string(trustedSignatures_.size()) + "):";
    for (auto it : trustedSignatures_) {
      packageString = packageString + "\n\t" + cs::Utils::byteStreamToHex(it.data(), it.size());
    }
    packageString = packageString + "\n" + "RoundSignatures(" + std::to_string(roundSignatures_.size()) + "):";
    for (auto it : roundSignatures_) {
      packageString = packageString + "\n\t" + cs::Utils::byteStreamToHex(it.data(), it.size());
    }
    return packageString;
  }

  cs::Bytes RoundPackage::bytesToSign() {
    refillToSign();
    cs::Bytes bytes(binaryRepresentation_.data(), binaryRepresentation_.data() + messageSize_);
    return bytes;
  }

  void RoundPackage::updatePoolMeta(const cs::PoolMetaInfo& meta) {
    poolMetaInfo_ = meta;
  }

  void RoundPackage::updateRoundTable(const cs::RoundTable& rt) {
    roundTable_ = rt;
  }

  void RoundPackage::updateRoundSignatures(const cs::Signatures signatures) {
    roundSignatures_ = signatures;

  }

  void RoundPackage::updatePoolSignatures(const cs::Signatures signatures) {
    poolSignatures_ = signatures;

  }

  void RoundPackage::updateTrustedSignatures(const cs::Signatures signatures) {
    trustedSignatures_ = signatures;
  }

  void RoundPackage::updateSmartSignatures(const std::vector<csdb::Pool::SmartSignature> smartSignatures) {
  }

  const cs::PoolMetaInfo RoundPackage::poolMetaInfo() {
    return poolMetaInfo_;
  }

  cs::RoundTable RoundPackage::roundTable() {
    return roundTable_;
  }

  cs::Signatures RoundPackage::roundSignatures() {
    return roundSignatures_;
  }

  cs::Signatures RoundPackage::poolSignatures() {
    return poolSignatures_;
  }

  cs::Signatures RoundPackage::trustedSignatures() {
    return trustedSignatures_;
  }

  size_t RoundPackage::messageLength()
  {
    return messageLength();
  }

  void RoundPackage::refillToSign() {
    size_t expectedMessageSize = roundTable_.confidants.size() * sizeof(cscrypto::PublicKey) + sizeof(size_t)
      + roundTable_.hashes.size() * sizeof(cscrypto::Hash) + sizeof(size_t)
      + poolMetaInfo_.timestamp.size() * sizeof(cs::Byte) + sizeof(size_t)
      + poolMetaInfo_.characteristic.mask.size() * sizeof(cs::Byte) + sizeof(size_t)
      + sizeof(size_t)
      + sizeof(cs::Hash) + sizeof(size_t)
      + poolMetaInfo_.realTrustedMask.size() + sizeof(size_t);

    binaryRepresentation_.clear();
    binaryRepresentation_.reserve(expectedMessageSize);
    cs::DataStream stream(binaryRepresentation_);
    uint8_t subRound = 0;
    uint8_t iteration = 0;
    stream << roundTable_.confidants;
    stream << poolMetaInfo_.realTrustedMask;
    stream << subRound_ << iteration;
    stream << roundTable_.hashes;
    stream << poolMetaInfo_.timestamp;
    stream << poolMetaInfo_.characteristic.mask;
    stream << poolMetaInfo_.sequenceNumber;
    stream << poolMetaInfo_.previousHash;
    //stream << lastSentRoundData_.poolMetaInfo.writerKey; -- we don't need to send this
    //cs::Bytes trustedList;
    //cs::DataStream tStream(trustedList);
    //tStream << newRoundTable.round;
    //tStream << newRoundTable.confidants;
    messageSize_ = binaryRepresentation_.size();
  }

}

