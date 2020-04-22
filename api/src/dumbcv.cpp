#include <dumbcv.hpp>

bool cs::DumbCv::addCVInfo(const cs::Signature& signature) {
    cs::Lock lock(mutex_);

    if (const auto& it = cvInfo_.find(signature); it != cvInfo_.end()) {
        return false;
    }

    cvInfo_[signature];
    return true;
}

void cs::DumbCv::sendCvSignal(const cs::Signature& signature, Condition condition, const csdb::TransactionID& id, const csdb::Amount fee) {
    cs::Lock lock(mutex_);

    if (auto it = cvInfo_.find(signature); it != cvInfo_.end()) {
        auto& [cv, flag, cond, i, f] = it->second;
        cond = condition;
        flag = true;
        i = id;
        f = fee;
        cv.notify_one();
    }
}

cs::DumbCv::Result cs::DumbCv::waitCvSignal(const cs::Signature& signature) {
    Result result;
    result.condition = cs::DumbCv::Condition::TimeOut;

    std::unique_lock lock(mutex_);

    if (auto it = cvInfo_.find(signature); it != cvInfo_.end()) {
        it->second.cv.wait_for(lock, std::chrono::seconds(kWaitTimeSec), [it]() -> bool {
            return it->second.condFlg;
        });

        if (it->second.condFlg) {
            result.condition = it->second.condition;
            result.id = it->second.id;
            result.fee = it->second.fee;
        }

        cvInfo_.erase(signature);
    }

    return result;
}
