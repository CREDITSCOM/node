#ifndef DUMBCV_HPP
#define DUMBCV_HPP

#include <atomic>
#include <map>
#include <lib/system/common.hpp>
#include <csdb/transaction.hpp>

namespace cs {
// for answer dumb transactions
class DumbCv {
    const size_t kWaitTimeSec = 15 * 60;

public:
    enum class Condition {
        Success,
        Rejected,
        TimeOut,
        Expired
    };

    struct Result {
        cs::DumbCv::Condition condition;
        csdb::TransactionID id{};
    };

    bool addCVInfo(const cs::Signature& signature);

    void sendCvSignal(const cs::Signature& signature, Condition condition, const csdb::TransactionID& id);
    void sendCvSignal(const cs::Signature& signature, Condition condition);

    DumbCv::Result waitCvSignal(const cs::Signature& signature);

private:
    struct CvInfo {
        std::condition_variable cv;
        std::atomic_bool condFlg{ false };
        std::atomic<Condition> condition { Condition::Success };
        csdb::TransactionID id{};
    };

    std::map<cs::Signature, CvInfo> cvInfo_;
    std::mutex mutex_;
};
}

#endif  // DUMBCV_HPP
