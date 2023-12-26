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
    known_contracts = reinterpret_cast<decltype(known_contracts)>(&contracts.known_contracts);
    exe_queue = reinterpret_cast<decltype(exe_queue)>(&contracts.exe_queue);
    blacklistedContracts_ = reinterpret_cast<decltype(blacklistedContracts_)>(&contracts.blacklistedContracts_);
    locked_contracts_ = reinterpret_cast<decltype(locked_contracts_)>(&contracts.locked_contracts);
    csdebug() << "Contracts bindings made";
}

void SmartContracts_Serializer::clear(const std::filesystem::path& rootDir) {
    known_contracts->clear();
    exe_queue->clear();
    blacklistedContracts_->clear();
    locked_contracts_->clear();
    save(rootDir);
}

void SmartContracts_Serializer::save(const std::filesystem::path& rootDir) {
    std::ofstream ofs(rootDir / kDataFileName, std::ios::binary);
    boost::archive::binary_oarchive oa(ofs);
    csdebug() << kLogPrefix << __func__;
    //oa << known_contracts->size();
    //for (auto it : *known_contracts) {
    //    csdebug() << "Contract: " << it.first.to_string();
    //    oa << it.first;
    //    oa << it.second;
    //}
    oa << known_contracts;
    oa << exe_queue;
    oa << blacklistedContracts_;
    oa << locked_contracts_;
    //printClassInfo();

}

std::string SmartContracts_Serializer::StateItem::transactionToString(const csdb::Transaction& tr) {
    std::ostringstream os;

    os << "Id: " << tr.id().to_string() << "\n";
    os << "InnerId: " << std::to_string(tr.innerID()) << "\n";
    os << "Amount: " << tr.amount().to_string() << "\n";
    os << "Sender: " << tr.source().to_string() << "\n";
    os << "Recipient: " << tr.target().to_string() << "\n";
    os << "Signature:" << cs::Utils::byteStreamToHex(tr.signature()) << "\n";
    os << "MaxFee: " << std::to_string(tr.max_fee().to_double()) << "\n";
    os << "CountedFee: " << std::to_string(tr.counted_fee().to_double()) << "\n";
    os << "User fields: "<<"\n";
    auto ufids = tr.user_field_ids();
    for (auto a : ufids) {
        auto ufld = tr.user_field(a);
        if (ufld.is_valid()) {
            switch (ufld.type()) {
            case ufld.Amount:
                os << "#" << static_cast<int>(a) << "(amount): " << ufld.value<csdb::Amount>().to_string();
                break;
            case ufld.Integer:
                os << "#" << static_cast<int>(a) << "(Integer): " << std::to_string(ufld.value<uint64_t>());
                break;
            case ufld.String:
                os << "#" << static_cast<int>(a) << "(String): " << ufld.value<std::string>();
                break;
            default:
                os << "#" << static_cast<int>(a) << "User field type not defined";
            }
        }
    }
    return os.str();
}

void SmartContracts_Serializer::printClassInfo() {
    csdebug() << kLogPrefix << __func__ << "> Known contracts (" << known_contracts->size() << "):";
    int i = 0;
    for (auto a : *known_contracts) {
        csdebug() << "Address:" << a.first.to_string();
        csdebug() << "Item #" << i << ": " << a.second.toString();
        ++i;

    }
    csdebug() << kLogPrefix << __func__ << "> Exe queue (" << exe_queue->size() << "):";

}

::cscrypto::Hash SmartContracts_Serializer::hash() {
    {
        std::ofstream ofs(kDataFileName, std::ios::binary);
        {
          boost::archive::binary_oarchive oa(
            ofs,
            boost::archive::no_header | boost::archive::no_codecvt
          );
          std::map<csdb::Address, StateItem> tmp(
            known_contracts->begin(),
            known_contracts->end()
          );
          //csdebug() << kLogPrefix << __func__;
          //printClassInfo();
          //oa << tmp.size();
          //for (auto it : tmp) {
          //    csdebug() << "Contract: " << it.first.to_string();
          //    oa << it.first;
          //    oa << it.second;
          //}
          oa << tmp;
          oa << exe_queue;
          oa << blacklistedContracts_;
          oa << locked_contracts_;
        }

    }
    
    auto result = SerializersHelper::getHashFromFile(kDataFileName);
    //std::filesystem::remove(kDataFileName);
    return result;
}

//void printIStream(const std::filesystem::path& rootDir) {
//    std::ifstream ifs(rootDir / kDataFileName);
//    std::string ret;
//    char c = ifs.get();
//
//    while (ifs.good()) {
//        ret += c;
//        c = ifs.get();
//    }
//    csdebug() << "loaded hex" << cs::Utils::byteStreamToHex(ret.data(), ret.size());
//}

void SmartContracts_Serializer::load(const std::filesystem::path& rootDir) {
    std::ifstream ifs(rootDir / kDataFileName, std::ios::binary);
    boost::archive::binary_iarchive ia(ifs);
    csdebug() << kLogPrefix << __func__;
    //size_t cSize;

    //ia >> cSize;
    //for (size_t i = 0ULL; i < cSize; ++i) {
    //    SmartContracts_Serializer::StateItem st;
    //    csdb::Address addr;
    //    ia >> addr;
    //    ia >> st;
    //    known_contracts->emplace(addr, st);
    //}
    ia >> known_contracts;
    ia >> exe_queue;
    ia >> blacklistedContracts_;
    ia >> locked_contracts_;
    //printClassInfo();

}
//TODO: insert own serialization for contracts to work properly:


}  // namespace cs
