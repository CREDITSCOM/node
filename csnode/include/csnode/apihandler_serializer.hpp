#pragma once

#include <filesystem>
#include <map>

#include <lib/system/concurrent.hpp>

namespace api {

class APIHandler;

} // namespace api

namespace cs {

class APIHandler_Serializer {
public:
    void bind(api::APIHandler&);
    void save(const std::filesystem::path& rootDir);
    void load(const std::filesystem::path& rootDir);
    void clear(const std::filesystem::path& rootDir);

    ::cscrypto::Hash hash();

private:
    cs::SpinLockable<std::map<csdb::TransactionID, SmartOperation>>* smart_operations;
    cs::SpinLockable<std::map<cs::Sequence, std::vector<csdb::TransactionID>>>* smarts_pending;
    cs::SpinLockable<std::map<csdb::Address, csdb::TransactionID>>* smart_origin;
    cs::SpinLockable<std::map<csdb::Address, smart_trxns_queue>>* smartLastTrxn_;
    cs::SpinLockable<std::map<cs::Signature, std::shared_ptr<smartHashStateEntry>>>* hashStateSL;
    cs::SpinLockable<std::map<csdb::Address, std::vector<csdb::TransactionID>>>* deployedByCreator_;
    cs::SpinLockable<std::map<cs::Sequence, api::Pool>>* poolCache;

    std::map<std::string, int64_t>* mExecuteCount_;
};

} // namespace cs
