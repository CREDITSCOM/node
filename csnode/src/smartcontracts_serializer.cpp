#include <fstream>
#include <sstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <solver/smartcontracts.hpp>
#include <csnode/smartcontracts_serializer.hpp>
#include <csnode/serializers_helper.hpp>

namespace {
const std::string kDataFileName = "smartcontracts.dat";
} // namespace

namespace cs {
void SmartContracts_Serializer::bind(SmartContracts& contract) {
    known_contracts = reinterpret_cast<decltype(known_contracts)>(&contract.known_contracts);
    exe_queue = reinterpret_cast<decltype(exe_queue)>(&contract.exe_queue);
}

void SmartContracts_Serializer::clear(const std::filesystem::path& rootDir) {
    known_contracts->clear();
    exe_queue->clear();
    save(rootDir);
}

void SmartContracts_Serializer::save(const std::filesystem::path& rootDir) {
    std::ofstream ofs(rootDir / kDataFileName);
    boost::archive::text_oarchive oa(ofs);
    oa << *known_contracts;
    oa << *exe_queue;
}

::cscrypto::Hash SmartContracts_Serializer::hash() {
    {
        std::ofstream ofs(kDataFileName);
        {
          boost::archive::text_oarchive oa(
            ofs,
            boost::archive::no_header | boost::archive::no_codecvt
          );
          std::map<csdb::Address, StateItem> tmp(
            known_contracts->begin(),
            known_contracts->end()
          );
          oa << tmp;
          oa << *exe_queue;
        }
    }

    auto result = SerializersHelper::getHashFromFile(kDataFileName);
    std::filesystem::remove(kDataFileName);
    return result;
}

void SmartContracts_Serializer::load(const std::filesystem::path& rootDir) {
    std::ifstream ifs(rootDir / kDataFileName);
    boost::archive::text_iarchive ia(ifs);
    ia >> *known_contracts;
    ia >> *exe_queue;
}
}  // namespace cs
