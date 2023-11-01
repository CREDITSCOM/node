#ifndef SMARTCONTRACTS_SERIALIZER_HPP
#define SMARTCONTRACTS_SERIALIZER_HPP
#include <filesystem>

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/unordered_set.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/split_member.hpp>

#include <csnode/transactionspacket.hpp>
#include <cscrypto/cscrypto.hpp>
#include <csdb/transaction.hpp>
#include <lib/system/common.hpp>

#include "address_serializer.hpp"

namespace cs {
class SmartContracts;
class SmartConsensus;
class csdb::Transaction;

class SmartContracts_Serializer {
public:
    void bind(SmartContracts&);
    void save(const std::filesystem::path& rootDir);
    void load(const std::filesystem::path& rootDir);
    void clear(const std::filesystem::path& rootDir);
    void printClassInfo();

    ::cscrypto::Hash hash();

    class Amount {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, [[maybe_unused]] const unsigned int version) {
            ar& integral_;
            ar& fraction_;
        }

        int32_t integral_;
        uint64_t fraction_;

    public:
        bool operator<(const Amount& other) const noexcept {
            return (integral_ < other.integral_) ? true : (integral_ > other.integral_) ? false : (fraction_ < other.fraction_);
        }

        bool operator>(const Amount& other) const noexcept {
            return (integral_ > other.integral_) ? true : (integral_ < other.integral_) ? false : (fraction_ > other.fraction_);
        }
    };

    class SmartContractRef {
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & sequence;
            ar & transaction;
        }
    public:
        std::string toString() {
            return std::to_string(sequence) + "." + std::to_string(transaction);
        }
        cs::Sequence getSequence() {
            return sequence;
        };

        size_t getTransaction() {
            return transaction;
        }
    private:
        cs::Sequence sequence;
        size_t transaction;
    };

    enum class PayableStatus : int {};
    enum class SmartContractStatus : int {};

    class StateItem {
        friend class boost::serialization::access;
    public:
        static std::string transactionToString(const csdb::Transaction& tr);
        std::string toString() {
        
            std::string res;
            res += "Payable_status: " + std::to_string(getIntPayable()) + "\n";
            res += "Ref deploy: " + ref_deploy.toString() + "\n";
            res += "Ref execute: " + ref_execute.toString() + "\n";
            res += "Ref cache: " + ref_cache.toString() + "\n";
            res += "Ref state: " + ref_state.toString() + "\n";
            res += "Deploy trx: \n" + transactionToString(deploy) + "\n";
            res += "Execute trx: \n" + transactionToString(execute) + "\n";
            return res;
        }
        PayableStatus getPayable() {
            return payable;
        }

        int getIntPayable() {
            return static_cast<int>(payable);
        }

        SmartContractRef getRefDeploy() {
            return ref_deploy;
        }

        SmartContractRef getRefExecute() {
            return ref_execute;
        }

        SmartContractRef getRefCache() {
            return ref_cache;
        }

        SmartContractRef getRefState() {
            return ref_state;
        }

        csdb::Transaction getDeployTransaction() {
            return deploy;
        }

        csdb::Transaction getExecuteTransaction() {
            return execute;
        }

        std::string getState() {
            return state;

        }

        std::unordered_map<std::string, std::unordered_map<csdb::Address, std::string>> getUses() {
            return uses;
        }

        void setPayable(PayableStatus& p) {
            payable = p;
        }

        void setPayable(int p) {
            payable = static_cast<PayableStatus>(p);
        }
        void setRefDeploy(SmartContractRef& r) {
            ref_deploy = r;
        }

        void setRefExecute(SmartContractRef& r) {
            ref_execute = r;
        }

         void setRefCache(SmartContractRef& r) {
            ref_cache = r;
        }

        void setRefState(SmartContractRef& r) {
            ref_state = r;
        }

        void setDeployTransaction(csdb::Transaction& tr) {
            deploy = tr;
        }

        void setExecuteTransaction(csdb::Transaction& tr) {
            execute = tr;
        }

        void setState(std::string& st) {
            state = st;


        }

        void setUses(std::unordered_map<std::string, std::unordered_map<csdb::Address, std::string>>& us) {
            uses = us;
        }

    private:


        //template<class Archive>
        //void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
        //    ar & payable;
        //    ar & ref_deploy;
        //    ar & ref_execute;
        //    ar & ref_state;
        //    ar & deploy;
        //    ar & execute;
        //    ar& state;
        //    ar& uses;

        //}
        template<class Archive>
        void save(Archive& ar, const unsigned int version) const {
            ar & payable;
            ar & ref_deploy;
            ar & ref_execute;
            ar & ref_state;
            ar & deploy.to_byte_stream();
            ar & execute.to_byte_stream();
            ar& state;
            ar& uses;
        }
        template<class Archive>
        void load(Archive& ar, const unsigned int version) {
            ar & payable;
            ar & ref_deploy;
            ar & ref_execute;
            ar & ref_state;

            cs::Bytes td;
            ar >> td;
            deploy = csdb::Transaction::from_binary(td);
            deploy.update_id(csdb::TransactionID(ref_deploy.getSequence(), ref_deploy.getTransaction()));

            csdebug() << "DeployTransaction: " << StateItem::transactionToString(deploy);


            cs::Bytes bytesExecute;
            ar >> bytesExecute;
            execute = csdb::Transaction::from_binary(bytesExecute);
            execute.update_id(csdb::TransactionID(ref_execute.getSequence(), ref_execute.getTransaction()));

            csdebug() << "ExecuteTransaction: " << StateItem::transactionToString(execute);

            ar& state;
            ar& uses;
        }
        BOOST_SERIALIZATION_SPLIT_MEMBER()
        PayableStatus payable;
        SmartContractRef ref_deploy;
        SmartContractRef ref_execute;
        SmartContractRef ref_cache;
        SmartContractRef ref_state;
        csdb::Transaction deploy{};
        csdb::Transaction execute{};
        std::string state;
        std::unordered_map<std::string, std::unordered_map<csdb::Address, std::string>> uses;
    };
//#pragma pack(push, 1)







 

    class ExecutionItem {
        friend class boost::serialization::access;
    public:
        SmartContractRef getRefStart() {
            return ref_start;
        }

        csdb::Transaction getTransaction() {
            return transaction;
        }

        Amount getAvailableFee() {
            return avail_fee;
        }

        Amount getNewStateFee() {
            return new_state_fee;
        }

        Amount getConsumedFee() {
            return consumed_fee;
        }

        std::vector<csdb::Address> getUses() {
            return  uses;
        }
        cs::TransactionsPacket getTransactionsPacket() {
            return result;
        }

        void setSmartContractRef(SmartContractRef& ref) {
            ref_start = ref;
        }

        void setTransaction(csdb::Transaction& tr) {
            transaction = tr;
        }

        void setAvailFee(Amount& fee) {
            avail_fee = fee;
        }

        void setNewStateFee(Amount& am) {
            new_state_fee = am;
        }

        void setConsumedFee(Amount& am) {
            consumed_fee = am;
        }

        void setUses(std::vector<csdb::Address>& us) {
            uses = us;
        }

        void setTransactionsPacket(cs::TransactionsPacket& pack) {
            result = pack;
        }
    private:

        //template<class Archive>
        //void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
        //    ar & ref_start;
        //    ar& transaction;
        //    ar& avail_fee;
        //    ar& new_state_fee;
        //    ar& consumed_fee;
        //    ar& uses;
        //    ar& result;
        //}
        template<class Archive>
        void save(Archive& ar, const unsigned int version) const {
            ar& ref_start;
            ar& transaction.to_byte_stream();
            ar& avail_fee;
            ar& consumed_fee;
            ar& uses;
            ar& result.toBinary();
        }
        template<class Archive>
        void load(Archive& ar, const unsigned int version) {
            ar& ref_start;

            cs::Bytes tr;
            ar >> tr;
            transaction = csdb::Transaction::from_binary(tr);
            transaction.update_id(csdb::TransactionID(ref_start.getSequence(), ref_start.getTransaction()));

            csdebug() << "StartTransaction: " << StateItem::transactionToString(transaction);

            ar& avail_fee;
            ar& consumed_fee;
            ar& uses;

            cs::Bytes packBytes;
            ar >> packBytes;
            result =cs::TransactionsPacket::fromBinary(packBytes);
            
        }
        BOOST_SERIALIZATION_SPLIT_MEMBER()



        SmartContractRef ref_start;
        csdb::Transaction transaction{};
        Amount avail_fee;
        Amount new_state_fee;
        Amount consumed_fee;
        std::vector<csdb::Address> uses;
        cs::TransactionsPacket result;
    };

    class QueueItem {
        friend class boost::serialization::access;
    public:
        std::vector<ExecutionItem> getExecutions() {
            return executions;
        }

        SmartContractStatus getContractStatus() {
            return status;
        }

        cs::Sequence enqueSeq() {
            return seq_enqueue;
        }

        cs::Sequence getStart() {
            return seq_start;
        }

        cs::Sequence getFinish() {
            return seq_finish;
        }

        csdb::Address getContractAddr() {
            return abs_addr;
        }

        bool getIsExecutor() {
            return is_executor;
        }

        bool getIsRejected() {
            return is_rejected;
        }

        void setExecution(std::vector<ExecutionItem>& execs) {
            executions = execs;
        }

        void setContractStatus(SmartContractStatus& st) {
            status = st;
        }
        void setEnqueue(cs::Sequence enq) {
            seq_enqueue = enq;
        }
        void setStart(cs::Sequence st) {
            seq_start = st;
        }

        void setFinish(cs::Sequence fin) {
            seq_finish = fin;
        }

        void setAbsAddr(csdb::Address& addr) {
            abs_addr = addr;
        }

        void setIsExecutor(bool ex) {
            is_executor = ex;
        }
        void setIsRejected(bool rej) {
            is_rejected = rej;
        }

    private:

        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
            ar & executions;
            ar& status;
            ar& seq_enqueue;
            ar & seq_start;
            ar& seq_finish;
            ar& abs_addr;
            ar& is_executor;
            ar& is_rejected;

        }

        std::vector<ExecutionItem> executions;
        SmartContractStatus status;
        cs::Sequence seq_enqueue;
        cs::Sequence seq_start;
        cs::Sequence seq_finish;
        csdb::Address abs_addr;
        bool is_executor;
        bool is_rejected;
        //std::unique_ptr<SmartConsensus> pconsensus;
    };

private:
    std::unordered_map<csdb::Address, StateItem> *known_contracts = nullptr;

    std::unordered_set<csdb::Address> *blacklistedContracts_ = nullptr;

    //std::unordered_set<csdb::Address> *locked_contracts;

    //std::vector<SmartContractRef> *uncompleted_contracts;

    std::list<QueueItem> *exe_queue = nullptr;
};
}  // namespace cs
#endif // SMARTCONTRACTS_SERIALIZER_HPP
