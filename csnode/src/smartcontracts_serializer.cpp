#include <fstream>
#include <sstream>
#include <exception>

//#include <boost/archive/text_oarchive.hpp>
//#include <boost/archive/text_iarchive.hpp>

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include <solver/smartcontracts.hpp>
#include <csnode/smartcontracts_serializer.hpp>
#include <csnode/serializers_helper.hpp>

#include "logger.hpp"

namespace {
const std::string kDataFileName = "smartcontracts.dat";
const std::string kLogPrefix = "SmartContracts_Serializer: ";
} // namespace

namespace cs {
void SmartContracts_Serializer::bind(SmartContracts& contracts) {
    contracts_ = &contracts;
    csdebug() << "Contracts bindings made";
}

void SmartContracts_Serializer::clear(const std::filesystem::path& rootDir) {
    contracts_->clear();
    save(rootDir);
}

void SmartContracts_Serializer::save(const std::filesystem::path& rootDir) {
    std::ofstream ofs(rootDir / kDataFileName, std::ios::binary);
    boost::archive::binary_oarchive oa(ofs);
    csdebug() << kLogPrefix << __func__;
    contracts_->printClassInfo();
    oa << contracts_->serialize();
}

::cscrypto::Hash SmartContracts_Serializer::hash() {
    {
        std::ofstream ofs(kDataFileName, std::ios::binary);
        {
          boost::archive::binary_oarchive oa(
            ofs,
            boost::archive::no_header | boost::archive::no_codecvt
          );
          csdebug() << kLogPrefix << __func__;
          contracts_->printClassInfo();
          oa << contracts_->serialize();
        }

    }
    
    auto result = SerializersHelper::getHashFromFile(kDataFileName);
    //std::filesystem::remove(kDataFileName);
    return result;
}

void SmartContracts_Serializer::load(const std::filesystem::path& rootDir) {
    std::ifstream ifs(rootDir / kDataFileName, std::ios::binary);
    boost::archive::binary_iarchive ia(ifs);
    csdebug() << kLogPrefix << __func__;
    Bytes data;
    ia >> data;
    contracts_->deserialize(data);
    contracts_->printClassInfo();

}
}  // namespace cs
