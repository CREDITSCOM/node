#include <dumbcv.hpp>

bool cs::DumbCv::addCVInfo(const cs::Signature& signature) {
    cs::Lock lock(mutex_);

    if (const auto& it = cvInfo_.find(signature); it != cvInfo_.end()) {
        return false;
    }

    cvInfo_[signature];
    return true;
}

void cs::DumbCv::sendCvSignal(const cs::Signature& signature, Condition condition) {
    cs::Lock lock(mutex_);

    if (auto it = cvInfo_.find(signature); it != cvInfo_.end()) {
        auto& [cv, flag, cond] = it->second;
        cond = condition;
        flag = true;
        cv.notify_one();
    }
}

cs::DumbCv::Condition cs::DumbCv::waitCvSignal(const cs::Signature& signature) {
    bool isTimeOver = false;
    Condition condition = cs::DumbCv::Condition::TimeOut;
    std::unique_lock lock(mutex_);

    if (auto it = cvInfo_.find(signature); it != cvInfo_.end()) {
        isTimeOver = it->second.cv.wait_for(lock, std::chrono::seconds(kWaitTimeMs), [it]() -> bool {
            return it->second.condFlg;
        });

        if (it->second.condFlg) {
            condition = it->second.condition;
        }

        cvInfo_.erase(signature);
    }

    return condition;
}
