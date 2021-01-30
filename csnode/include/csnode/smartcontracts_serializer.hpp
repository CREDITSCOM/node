#ifndef SMARTCONTRACTS_SERIALIZER_HPP
#define SMARTCONTRACTS_SERIALIZER_HPP
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/map.hpp>

#include <csnode/transactionspacket.hpp>
#include <csdb/transaction.hpp>
#include <lib/system/common.hpp>

#include "address_serializer.hpp"

namespace cs {
class SmartContracts;
class SmartConsensus;

class SmartContracts_Serializer {
public:
    void bind(SmartContracts&);
    void save();
    void load();

private:
    class SmartContractRef {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & sequence;
            ar & transaction;
        }

        cs::Sequence sequence;
        size_t transaction;
    };

    enum class PayableStatus : int {};
    enum class SmartContractStatus : int {};

    class StateItem {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & payable;
            ar & ref_deploy;
            ar & ref_execute;
            ar & ref_state;
        }

        PayableStatus payable;
        SmartContractRef ref_deploy;
        SmartContractRef ref_execute;
        SmartContractRef ref_cache;
        SmartContractRef ref_state;
        csdb::Transaction deploy{};
        csdb::Transaction execute{};
        std::string state;
        std::map<std::string, std::map<csdb::Address, std::string>> uses;
    };

    class ExecutionItem {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & ref_start;
        }

        SmartContractRef ref_start;
        csdb::Transaction transaction{};
        csdb::Amount avail_fee;
        csdb::Amount new_state_fee;
        csdb::Amount consumed_fee;
        std::vector<csdb::Address> uses;
        cs::TransactionsPacket result;
    };

    class QueueItem {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & executions;
            ar & seq_start;
        }

        std::vector<ExecutionItem> executions;
        SmartContractStatus status;
        cs::Sequence seq_enqueue;
        cs::Sequence seq_start;
        cs::Sequence seq_finish;
        csdb::Address abs_addr;
        bool is_executor;
        bool is_rejected;
        std::unique_ptr<SmartConsensus> pconsensus;
    };

    std::map<csdb::Address, StateItem> *known_contracts = nullptr;
    std::list<QueueItem> *exe_queue = nullptr;
};
}  // namespace cs
#endif // SMARTCONTRACTS_SERIALIZER_HPP
