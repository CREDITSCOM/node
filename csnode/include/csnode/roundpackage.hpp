#ifndef ROUNDPACKAGE_HPP
#define ROUNDPACKAGE_HPP

#include <nodecore.hpp>

namespace cs {
class RoundPackage {
public:
    RoundPackage();

    cs::Bytes toBinary();
    cs::Byte subRound();
    bool fromBinary(const cs::Bytes& bytes, cs::RoundNumber rNum, cs::Byte subRound);

    std::string toString();
    cs::Bytes bytesToSign();
    cs::RoundTable roundTable();
    const cs::PoolMetaInfo poolMetaInfo();

    void updateRoundTable(const cs::RoundTable& rt);
    void updatePoolMeta(const cs::PoolMetaInfo& meta);
    void updateRoundSignatures(const cs::Signatures signatures);
    void updatePoolSignatures(const cs::Signatures signatures);
    void updateTrustedSignatures(const cs::Signatures signatures);

    cs::Signatures roundSignatures();
    cs::Signatures poolSignatures();
    cs::Signatures trustedSignatures();
    size_t messageLength();

    cs::Byte iteration() {
        return iteration_;
    }

    cs::Byte subround() {
        return subRound_;
    }

    // store sender key with round package
    void setSenderNode(const cs::PublicKey& sender);
    
    bool hasSender() const {
        return bool(sender_);
    }

    bool getSender(cs::PublicKey& sender) const;

private:
    std::string name() {
        return "RoundPackage> ";
    }

    void refillToSign();

    cs::RoundTable roundTable_;
    cs::PoolMetaInfo poolMetaInfo_;  // confirmations sent in rt are confirmations for next pool

    cs::Signatures roundSignatures_;
    cs::Signatures poolSignatures_;
    cs::Signatures trustedSignatures_;

    std::vector<csdb::Pool::SmartSignature> smartSignatures_;

    cs::Bytes binaryRepresentation_;
    size_t messageSize_ = 0;
    cs::Byte iteration_ = 0U;
    cs::Byte subRound_ = 0U;

    std::shared_ptr<cs::PublicKey> sender_;
};
}  // namespace cs

#endif // ROUNDPACKAGE_HPP
