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
    oa << known_contracts->size();
    for (auto it : *known_contracts) {
        csdebug() << "Contract: " << it.first.to_string();
        oa << it.first;
        oa << it.second;
    }
    
    //size_t queueSize = exe_queue->size();
    oa << exe_queue;// queueSize;
    //auto qIt = exe_queue->begin();
    //for (size_t i = 0; i < queueSize; ++i) {
    //    oa << *qIt++;
    //}
    
    size_t bSize = blacklistedContracts_->size();
    oa << bSize;
    auto bIt = blacklistedContracts_->begin();
    for (size_t i = 0; i < bSize; ++i) {
        oa << *bIt++;
    }
    
    size_t lSize = locked_contracts_->size();
    oa << lSize;
    auto lIt = locked_contracts_->begin();
    for (size_t i = 0; i < lSize; ++i) {
        oa << *lIt++;
    }
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
          csdebug() << kLogPrefix << __func__;
          oa << tmp.size();
          for (auto it : tmp) {
              csdebug() << "Contract: " << it.first.to_string();
              oa << it.first;
              oa << it.second;
          }
          
          //size_t queueSize = exe_queue->size();
          oa << exe_queue;// queueSize;
          //auto qIt = exe_queue->begin();
          //for (size_t i = 0; i < queueSize; ++i) {
          //    oa << *qIt++;
          //}
          
          size_t bSize = blacklistedContracts_->size();
          oa << bSize;
          auto bIt = blacklistedContracts_->begin();
          for (size_t i = 0; i < bSize; ++i) {
              oa << *bIt++;
          }
          
          size_t lSize = locked_contracts_->size();
          oa << lSize;
          auto lIt = locked_contracts_->begin();
          for (size_t i = 0; i < lSize; ++i) {
              oa << *lIt++;
          }
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
    size_t kSize = 0;
    ia >> kSize;
    for (size_t i = 0; i < kSize; ++i){
        csdb::Address addr;
        StateItem sIt;
        ia >> addr;
        ia >> sIt;
        known_contracts->emplace(addr, sIt);
    }
    //size_t queueSize = 0;
    ia >> exe_queue;
    //for (size_t i = 0; i < queueSize; ++i) {
    //    QueueItem eIt;
    //    SmartContracts::QueueItem qIt;
    //    ia >> eIt;
    //    exe_queue->push_back(qIt);
    //}
    
    size_t bSize = 0;
    ia >> bSize;
    for (size_t i = 0; i < bSize; ++i) {
        csdb::Address addr;
        ia >> addr;
        blacklistedContracts_->insert(addr);
    }

    size_t lSize = locked_contracts_->size();
    ia >> lSize;
    for (size_t i = 0; i < lSize; ++i) {
        csdb::Address addr;
        ia >> addr;
        locked_contracts_->insert(addr);
    }

}
}  // namespace cs
