#include <csdb/pool.hpp>
#include <csnode/nodeutils.hpp>

namespace cs {
/*static*/
bool NodeUtils::checkGroupSignature(const cs::ConfidantsKeys& confidants, const cs::Bytes& mask, const cs::Signatures& signatures, const cs::Hash& hash) {
    if (confidants.size() == 0) {
        csdebug() << "The number of confidants is 0";
        return false;
    }
    if (confidants.size() != mask.size()) {
        cserror() << "The number of confidants doesn't correspond the mask size";
        return false;
    }

    size_t signatureCount = 0;
    for (auto& it : mask) {
        if (it == cs::ConfidantConsts::InvalidConfidantIndex) {
            continue;
        }
        ++signatureCount;
    }

    if (signatures.size() != signatureCount) {
        cserror() << "The number of signatures doesn't correspond the mask value";

        std::string realTrustedString;

        for (auto& i : mask) {
            realTrustedString = realTrustedString + "[" + std::to_string(int(i)) + "] ";
        }

        csdebug() << "Mask: " << realTrustedString << ", Signatures: ";
        for (auto& it : signatures) {
            csdebug() << cs::Utils::byteStreamToHex(it);
        }

        return false;
    }

    signatureCount = 0;
    size_t cnt = 0;
    bool validSig = true;
    csdebug() << "BlockChain> Hash: " << cs::Utils::byteStreamToHex(hash);
    for (auto it : mask) {
        if (it != cs::ConfidantConsts::InvalidConfidantIndex) {
            if (cscrypto::verifySignature(signatures[signatureCount], confidants[cnt], hash.data(), hash.size())) {
                csdebug() << "BlockChain> Signature of [" << cnt << "] is valid";
                ++signatureCount;
            }
            else {
                csdebug() << "BlockChain> Signature of [" << cnt << "] is NOT VALID: " << cs::Utils::byteStreamToHex(signatures[signatureCount]);
                validSig = false;
                ++signatureCount;
            }
        }
        ++cnt;
    }
    if (!validSig) {
        csdebug() << "Some signatures are not valid";
        return false;
    }
    else {
        csdebug() << "The signatures are valid";
        return true;
    }
}

/*static*/
size_t NodeUtils::realTrustedValue(const cs::Bytes& mask) {
    size_t cnt = 0;
    for (auto it : mask) {
        if (it != cs::ConfidantConsts::InvalidConfidantIndex) {
            ++cnt;
        }
    }
    return cnt;
}

/*static*/
cs::Bytes NodeUtils::getTrustedMask(const csdb::Pool& block) {
    if (!block.is_valid()) {
        return cs::Bytes{};
    }
    return cs::Utils::bitsToMask(block.numberTrusted(), block.realTrusted());
}
}  // namespace cs
