#include <fstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <solver/smartcontracts.hpp>
#include <csnode/smartcontracts_serializer.hpp>

namespace cs {
void SmartContracts_Serializer::bind(SmartContracts& contract) {
    known_contracts = reinterpret_cast<decltype(known_contracts)>(&contract.known_contracts);
    exe_queue = reinterpret_cast<decltype(exe_queue)>(&contract.exe_queue);
}

void SmartContracts_Serializer::save() {
    std::ofstream ofs("walletsids.dat");
    boost::archive::text_oarchive oa(ofs);
    oa << *known_contracts;
    oa << *exe_queue;
}

void SmartContracts_Serializer::load() {
    std::ifstream ifs("walletsids.dat");
    boost::archive::text_iarchive ia(ifs);
    ia >> *known_contracts;
    ia >> *exe_queue;
}
}  // namespace cs
