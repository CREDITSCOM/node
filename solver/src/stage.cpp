#include <stage.hpp>
#include <csdb/amount.hpp>
#include <csdb/currency.hpp>
#include <csnode/datastream.hpp>
#include <lib/system/utils.hpp>
#include <csnode/datastream.hpp>
#include <conveyer.hpp>
#include <cscrypto/cscrypto.hpp>
namespace cs {
    TrustedMask::TrustedMask() {}

    TrustedMask::TrustedMask(uint8_t size) {
        mask_.resize(static_cast<size_t>(size));
        std::fill(mask_.begin(), mask_.end(), cs::ConfidantConsts::InvalidConfidantIndex);
        isReadOnly_ = false;
    }

    void TrustedMask::init(uint8_t size)
    {
        mask_.resize(static_cast<size_t>(size));
        std::fill(mask_.begin(), mask_.end(), cs::ConfidantConsts::InvalidConfidantIndex);
        isReadOnly_ = false;
    }

    void TrustedMask::init(const std::vector<uint8_t>& newMask)
    {
        mask_.resize(newMask.size());
        mask_ = newMask;
        isReadOnly_ = false;
    }

    std::string TrustedMask::toString() {
        std::string realTrustedString;
        for (auto& i : mask_) {
            realTrustedString = realTrustedString + "[" + (i != 255U ? std::to_string(static_cast<int>(i)) : "X") + "] ";
        }
        return realTrustedString;
    }

    std::string TrustedMask::toString(std::vector<uint8_t> mask) {
        std::string realTrustedString;
        for (auto& i : mask) {
            realTrustedString = realTrustedString + "[" + (i != 255U ? std::to_string(static_cast<int>(i)) : "X") + "] ";
        }
        return realTrustedString;
    }

    void TrustedMask::setMask(uint8_t index, uint8_t value) {
        if (static_cast<size_t>(index) < mask_.size()) {
            mask_[static_cast<size_t>(index)] = value;
        }
    }

    uint8_t TrustedMask::trustedSize() {
        return static_cast<uint8_t>(mask_.size() - std::count(mask_.cbegin(), mask_.cend(), cs::ConfidantConsts::InvalidConfidantIndex));
    }

    uint8_t TrustedMask::trustedSize(const std::vector<uint8_t>& mask)
    {
        return static_cast<uint8_t>(mask.size() - std::count(mask.cbegin(), mask.cend(), cs::ConfidantConsts::InvalidConfidantIndex));
    }

    void StageOne::toBytes() {
        messageBytes.clear();
        size_t expectedMessageSize = sizeof(sender)
            + sizeof(hash)
            + sizeof(trustedCandidates.size())
            + sizeof(PublicKey) * trustedCandidates.size()
            + sizeof(hashesCandidates.size())
            + sizeof(Hash) * hashesCandidates.size()
            + sizeof(roundTimeStamp.size())
            + roundTimeStamp.size();

        //csmeta(csdebug) << "Stage one R - " << cs::Conveyer::instance().currentRoundNumber() << cs::StageOne::toString(this);

        messageBytes.reserve(expectedMessageSize);

        cs::DataStream stream(messageBytes);
        stream << sender;
        stream << hash;
        stream << trustedCandidates;
        stream << hashesCandidates;
        stream << roundTimeStamp;

        csdebug() << "Stage one Message R-" << cs::Conveyer::instance().currentRoundNumber() << "[" << static_cast<int>(sender)
            << "]: " << cs::Utils::byteStreamToHex(messageBytes.data(), messageBytes.size());
    }


    std::string StageOne::toString(const StageOne stage) {
        return (", Hash: " + cs::Utils::byteStreamToHex(stage.hash.data(), stage.hash.size()) + ", Sender: " + std::to_string(static_cast<int>(stage.sender))
            + ", Cand Amount: " + std::to_string(stage.trustedCandidates.size())
            + ", Hashes Amount: " + std::to_string(stage.hashesCandidates.size())
            + ", Time Stamp: " + stage.roundTimeStamp);
    }

    void StageTwo::toBytes() {
        messageBytes.clear();
        const size_t confidantsCount = cs::Conveyer::instance().confidantsCount();
        const size_t stageBytesSize = sizeof(sender)
            + sizeof(size_t) // count of signatures
            + sizeof(size_t) // count of hashes
            + (sizeof(cs::Signature) + sizeof(cs::Hash)) * confidantsCount; // signature + hash items

        messageBytes.reserve(stageBytesSize);

        cs::DataStream stream(messageBytes);
        stream << sender;
        stream << signatures;
        stream << hashes;
    }

    std::string StageThree::toString(const StageThree stage) {
        std::string trustedString = TrustedMask::toString(stage.realTrustedMask);
        return ("     SENDER = " + std::to_string(static_cast<int>(stage.sender))
            + ", WRITER = " + std::to_string(static_cast<int>(stage.writer)) + ", RealTrusted = " + trustedString + '\n'
            + "     BlockHash = " + cs::Utils::byteStreamToHex(stage.blockHash) + '\n'
            + "     BlockSign = " + cs::Utils::byteStreamToHex(stage.blockSignature) + '\n'
            + "     RoundHash = " + cs::Utils::byteStreamToHex(stage.roundHash) + "\n"
            + "     RoundSign = " + cs::Utils::byteStreamToHex(stage.roundSignature) + '\n'
            + "     TrustHash = " + cs::Utils::byteStreamToHex(stage.trustedHash) + '\n'
            + "     TrustSign = " + cs::Utils::byteStreamToHex(stage.trustedSignature));
    }


    void StageThree::toBytes() {
        const size_t stageSize = 2 * sizeof(uint8_t) + 3 * sizeof(cs::Signature) + realTrustedMask.size();
        messageBytes.clear();
        messageBytes.reserve(stageSize);

        cs::DataStream stream(messageBytes);
        stream << sender;
        stream << writer;
        stream << iteration;
        stream << blockSignature;
        stream << roundSignature;
        stream << trustedSignature;
        stream << realTrustedMask;
    }

    bool StageOneSmarts::fillBinary()
    {
        if (message.size() > 0) {
            message.clear();
        }

        messageHash.fill(0);
        if (sender == cs::ConfidantConsts::InvalidConfidantIndex) {
            return false;
        }
        if (id == 0) {
            return false;
        }
        if (fees.size() == 0) {
            return false;
        }
        std::string a = " ";//fees
        DataStream stream(message);
        stream << sender;
        stream << id;
        //bad implementation
        stream << fees.size(); //fees
        for (size_t i = 0; i < fees.size(); ++i) {
            stream << fees[i].integral() << fees[i].fraction();
        }
        stream << hash;

        cs::Bytes messageToSign;
        messageToSign.reserve(sizeof(cs::Hash));
        // hash of message
        messageHash = cscrypto::calculateHash(message.data(), message.size());
        return true;
    }

    bool StageOneSmarts::fillFromBinary()
    {
        std::string a; //fees
        DataStream stream(message.data(), message.size());
        stream >> sender; //is not larger than confidants number
        stream >> id; //is in the range of current smart IDs
        //bad implementation
        size_t fees_size;
        stream >> fees_size; //fees is equal the conf amount
 
        int32_t fee_integral;
        uint64_t fee_fracture;
        for (size_t i = 0; i < fees_size; ++i) {
            fee_integral = 0;
            fee_fracture = 0;
            stream >> fee_integral >> fee_fracture;
            if (fee_integral < 0 || fee_fracture > csdb::Amount::AMOUNT_MAX_FRACTION) {
                csdebug() << __func__ << ": Invalid fee value: "  << fee_integral << ", " << fee_fracture;
                return false;
            }
            csdb::Amount fee{ fee_integral, fee_fracture, csdb::Amount::AMOUNT_MAX_FRACTION };
            fees.push_back(fee);
        }
        stream >> hash;
        return true;
    }

    Bytes StageTwoSmarts::toBinary()
    {
        return Bytes();
    }

    bool StageTwoSmarts::fromBinary(Bytes /*msg*/, StageTwoSmarts & /*stage*/)
    {
        return false;
    }

    Bytes StageThreeSmarts::toBinary()
    {
        return Bytes();
    }

    bool StageThreeSmarts::fromBinary(Bytes /*msg*/, StageThreeSmarts & /*stage*/)
    {
        return false;
    }
}