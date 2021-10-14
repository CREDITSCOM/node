#pragma once

#include <filesystem>
#include <map>
#include <string>

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/vector.hpp>

#include <cscrypto/cscrypto.hpp>
#include <lib/system/concurrent.hpp>
#include <lib/system/common.hpp>

#include "address_serializer.hpp"

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
    class TransactionID {
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & pool_seq_;
            ar & index_;
        }

        cs::Sequence pool_seq_;
        cs::Sequence index_;
    };

    struct SmartOperation {
        enum class State : uint8_t {
            Pending,
            Success,
            Failed
        };

        State state = State::Pending;
        TransactionID stateTransaction;

        bool hasRetval : 1;
        bool returnsBool : 1;
        bool boolResult : 1;

        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & state;
            ar & stateTransaction;

            if (Archive::is_saving::value) {
                bool tmp = hasRetval;
                ar & tmp;
                tmp = returnsBool;
                ar & tmp;
                tmp = boolResult;
                ar & tmp;
            }
            else {
                bool tmp;
                ar & tmp;
                hasRetval = tmp ? 1 : 0;
                ar & tmp;
                returnsBool = tmp ? 1 : 0;
                ar & tmp;
                boolResult = tmp ? 1 : 0;
            }
        }
    };

    SpinLockable<std::map<TransactionID, SmartOperation>>* smart_operations                  = nullptr;
    SpinLockable<std::map<cs::Sequence, std::vector<TransactionID>>>* smarts_pending         = nullptr;
    SpinLockable<std::map<csdb::Address, TransactionID>>* smart_origin                       = nullptr;
    SpinLockable<std::map<csdb::Address, std::vector<TransactionID>>>* deployedByCreator_    = nullptr;

    std::map<std::string, int64_t>* mExecuteCount_ = nullptr;
};

} // namespace cs
