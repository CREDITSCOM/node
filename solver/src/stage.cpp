#include <stage.hpp>
#include <csdb/amount.hpp>
#include <csdb/currency.hpp>
#include <csnode/datastream.hpp>

#include <cscrypto/cscrypto.hpp>
namespace cs {
 
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
        stream >> sender;
        stream >> id;
        //bad implementation
        size_t fees_size;
        stream >> fees_size; //fees
        int32_t fee_integral;
        uint64_t fee_fracture;
        for (size_t i = 0; i < fees_size; ++i) {
            fee_integral = 0;
            fee_fracture = 0;
            stream >> fee_integral >> fee_fracture;
            csdb::Amount fee{ fee_integral, fee_fracture, csdb::Amount::AMOUNT_MAX_FRACTION };
            fees.push_back(fee);
        }
        stream >> hash;
        return false;
    }

    Bytes StageTwoSmarts::toBinary()
    {
        return Bytes();
    }

    bool StageTwoSmarts::fromBinary(Bytes message, StageTwoSmarts & stage)
    {
        return false;
    }

    Bytes StageThreeSmarts::toBinary()
    {
        return Bytes();
    }

    bool StageThreeSmarts::fromBinary(Bytes message, StageThreeSmarts & stage)
    {
        return false;
    }
}