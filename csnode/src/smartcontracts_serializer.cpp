#include <fstream>
#include <sstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <solver/smartcontracts.hpp>
#include <csnode/smartcontracts_serializer.hpp>

namespace cs {
void SmartContracts_Serializer::bind(SmartContracts& contract) {
    known_contracts = reinterpret_cast<decltype(known_contracts)>(&contract.known_contracts);
    exe_queue = reinterpret_cast<decltype(exe_queue)>(&contract.exe_queue);
}

void SmartContracts_Serializer::clear() {
    known_contracts->clear();
    exe_queue->clear();
    save();
}

void SmartContracts_Serializer::save() {
    std::ofstream ofs("smartcontracts.dat");
    boost::archive::text_oarchive oa(ofs);
    oa << *known_contracts;
    oa << *exe_queue;
}

::cscrypto::Hash SmartContracts_Serializer::hash() {
    std::ostringstream ofs;
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
    auto data = ofs.str();
    return ::cscrypto::calculateHash(
      (const ::cscrypto::Byte*)data.data(),
      data.size()
    );
}

void SmartContracts_Serializer::load() {
    std::ifstream ifs("smartcontracts.dat");
    boost::archive::text_iarchive ia(ifs);
    ia >> *known_contracts;
    ia >> *exe_queue;
}
}  // namespace cs
