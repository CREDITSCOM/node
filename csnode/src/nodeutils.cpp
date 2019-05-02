#include <csdb/pool.hpp>
#include <csnode/nodeutils.hpp>

namespace
{
  const char * log_prefix = "Node: ";
}

namespace cs {
/*static*/
bool NodeUtils::checkGroupSignature(const cs::ConfidantsKeys& confidants, const cs::Bytes& mask, const cs::Signatures& signatures, const cs::Hash& hash) {
    if (confidants.size() == 0) {
        csdebug() << log_prefix << "the number of confidants is 0";
        return false;
    }
    if (confidants.size() != mask.size()) {
        cserror() << log_prefix << "the number of confidants doesn't correspond the mask size";
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
        cserror() << log_prefix << "the number of signatures doesn't correspond the mask value";

        std::string realTrustedString;

        for (auto& i : mask) {
            realTrustedString = realTrustedString + "[" + std::to_string(int(i)) + "] ";
        }

        csdebug() << log_prefix << "mask: " << realTrustedString << ", signatures: ";
        for (auto& it : signatures) {
            csdebug() << '\t' << cs::Utils::byteStreamToHex(it);
        }

        return false;
    }

    signatureCount = 0;
    size_t cnt = 0;
    bool validSig = true;
    size_t cntValid = 0;
    size_t cntInvalid = 0;
    csdebug() << log_prefix << "hash: " << cs::Utils::byteStreamToHex(hash);
    for (auto it : mask) {
        if (it != cs::ConfidantConsts::InvalidConfidantIndex) {
            if (cscrypto::verifySignature(signatures[signatureCount], confidants[cnt], hash.data(), hash.size())) {
                csdetails() << log_prefix << "signature of [" << cnt << "] is valid";
                ++signatureCount;
                ++cntValid;
            }
            else {
                csdebug() << log_prefix << "signature of [" << cnt << "] is NOT VALID: " << cs::Utils::byteStreamToHex(signatures[signatureCount]);
                validSig = false;
                ++signatureCount;
                ++cntInvalid;
            }
        }
        ++cnt;
    }
    if (!validSig) {
        csdebug() << log_prefix << "signatures (" << cntInvalid << ") are not valid";
        return false;
    }
    else {
        csdebug() << log_prefix << "every " << cntValid << " signatures are valid";
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
