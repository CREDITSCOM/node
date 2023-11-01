#include <fstream>
#include <sstream>
#include <exception>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

//#include <boost/archive/binary_oarchive.hpp>
//#include <boost/archive/binary_iarchive.hpp>

#include <solver/smartcontracts.hpp>
#include <csnode/smartcontracts_serializer.hpp>
#include <csnode/serializers_helper.hpp>

#include "logger.hpp"

namespace {
const std::string kDataFileName = "smartcontracts.dat";
const std::string kLogPrefix = "SmartContracts_Serializer: ";
} // namespace

namespace cs {
void SmartContracts_Serializer::bind(SmartContracts& contract) {
    known_contracts = reinterpret_cast<decltype(known_contracts)>(&contract.known_contracts);
    exe_queue = reinterpret_cast<decltype(exe_queue)>(&contract.exe_queue);
    //blacklistedContracts_ = reinterpret_cast<decltype(blacklistedContracts_)>(&contract.blacklistedContracts_);
}

void SmartContracts_Serializer::clear(const std::filesystem::path& rootDir) {
    known_contracts->clear();
    exe_queue->clear();
    //blacklistedContracts_->clear();
    save(rootDir);
}

void SmartContracts_Serializer::save(const std::filesystem::path& rootDir) {
    std::ofstream ofs(rootDir / kDataFileName, std::ios::binary);
    boost::archive::text_oarchive oa(ofs);
    csdebug() << kLogPrefix << __func__;
    oa << known_contracts;
    oa << exe_queue;
   // oa << known_contracts->size();
    //for (auto a : *known_contracts) {
    //    oa << a.first;
    //    oa << a.second.getPayable();
    //    oa << a.second.getRefDeploy();
    //    oa << a.second.getRefExecute();
    //    oa << a.second.getRefCache();
    //    oa << a.second.getRefState();
    //    oa << a.second.getDeployTransaction().to_byte_stream();
    //    oa << a.second.getExecuteTransaction().to_byte_stream();
    //    oa << a.second.getState();
    //    oa << a.second.getUses();
    //}
    //oa << exe_queue->size();
    //for (auto a : *exe_queue) {
    //    for (auto ex : a.getExecutions()) {

    //    }

    //        ar& ref_start;
    //    ar& transaction;
    //    ar& avail_fee;
    //    ar& new_state_fee;
    //    ar& consumed_fee;
    //    ar& uses;
    //    ar& result;

    //        r& executions;
    //    ar& status;
    //    ar& seq_enqueue;
    //    ar& seq_start;
    //    ar& seq_finish;
    //    ar& abs_addr;
    //    ar& is_executor;
    //    ar& is_rejected;
    //}
    printClassInfo();
   // oa << *blacklistedContracts_;

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
    csdebug() << kLogPrefix << __func__ << ": Known contracts";
    int i = 0;
    for (auto a : *known_contracts) {
        csdebug() << "Address:" << a.first.to_string();
        csdebug() << "Item #" << i << ": " << a.second.toString();
        ++i;

    }

}

::cscrypto::Hash SmartContracts_Serializer::hash() {
    {
        std::ofstream ofs(kDataFileName, std::ios::binary);
        {
          boost::archive::text_oarchive oa(
            ofs,
            boost::archive::no_header | boost::archive::no_codecvt
          );
          std::map<csdb::Address, StateItem> tmp(
            known_contracts->begin(),
            known_contracts->end()
          );
          csdebug() << kLogPrefix << __func__;
          printClassInfo();
          oa << known_contracts;
          //oa << known_contracts->size();
          //for (auto a : *known_contracts) {
          //    oa << a.first;
          //    oa << static_cast<int>(a.second.getPayable());
          //    oa << a.second.getRefDeploy();
          //    oa << a.second.getRefExecute();
          //    oa << a.second.getRefCache();
          //    oa << a.second.getRefState();
          //    oa << a.second.getDeployTransaction().to_byte_stream();
          //    oa << a.second.getExecuteTransaction().to_byte_stream();
          //    oa << a.second.getState();
          //    oa << a.second.getUses();
          //}
          oa << exe_queue;
          //for (auto a : *exe_queue) {

          //}
          //oa << *blacklistedContracts_;
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
    boost::archive::text_iarchive ia(ifs);
    csdebug() << kLogPrefix << __func__;
    
    ia >> known_contracts;
    ia >> exe_queue;
    //size_t kSize;
    //ia >> kSize;
    //csdebug() << "Contracts: " << kSize;
    //for (size_t i = 0; i < kSize; ++i) {
    //    StateItem sa;
    //    csdb::Address addr;
    //    //std::string strAddress;
    //    ia >> addr;
    //    //addr.from_string(strAddress);
    //    csdebug() << "Address: " << addr.to_string();

    //    int intP;
    //    ia >> intP;
    //    sa.setPayable(intP);

    //    SmartContractRef dep;
    //    ia >> dep;
    //    sa.setRefDeploy(dep);
    //    csdebug() << "Dep: " << dep.toString();

    //    SmartContractRef exec;
    //    ia >> exec;
    //    sa.setRefExecute(exec);
    //    csdebug() << "Exec: " << exec.toString();

    //    SmartContractRef cache;
    //    ia >> cache;
    //    sa.setRefCache(cache);
    //    csdebug() << "Cache: " << cache.toString();

    //    SmartContractRef refState;
    //    ia >> refState;
    //    sa.setRefState(refState);
    //    csdebug() << "RefState: " << refState.toString();


    //    cs::Bytes td;
    //    ia >> td;
    //    csdebug() << "DeployTransaction binary: " << cs::Utils::byteStreamToHex(td);
    //    csdb::Transaction  deploy = csdb::Transaction::from_binary(td);
    //    deploy.update_id(csdb::TransactionID(dep.getSequence(), dep.getTransaction()));

    //    sa.setDeployTransaction(deploy);
    //    csdebug() << "DeployTransaction: " << StateItem::transactionToString(deploy);


    //    cs::Bytes bytesExecute;
    //    //ia >> trSize;
    //    //bytesExecute.resize(trSize);
    //    ia >> bytesExecute;
    //    csdb::Transaction execute = csdb::Transaction::from_binary(bytesExecute);
    //    execute.update_id(csdb::TransactionID(exec.getSequence(), exec.getTransaction()));
    //    sa.setExecuteTransaction(execute);
    //    csdebug() << "ExecuteTransaction: " << StateItem::transactionToString(execute);

    //    std::string state;
    //    ia >> state;
    //    sa.setState(state);
    //    csdebug() << "State: " << state;

    //    std::unordered_map<std::string, std::unordered_map<csdb::Address, std::string>> uses;
    //    ia >> uses;
    //    sa.setUses(uses);
    //    
    //    known_contracts->try_emplace(addr, sa);
    //}
    //ia >> kSize;
    //for (size_t i = 0; i < kSize; ++i) {

    //}
    printClassInfo();

    //ia >> *blacklistedContracts_;
}
//TODO: insert own serialization for contracts to work properly:


}  // namespace cs
