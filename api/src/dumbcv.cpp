#include <dumbcv.hpp>

bool cs::DumbCv::addCVInfo(const cs::Signature& signature) {
    cs::Lock lock(mutex_);

    if (const auto& it = cvInfo_.find(signature); it != cvInfo_.end()) {
        return false;
    }

    cvInfo_[signature];
    return true;
}

void cs::DumbCv::sendCvSignal(const cs::Signature& signature) {
    cs::Lock lock(mutex_);

    if (auto it = cvInfo_.find(signature); it != cvInfo_.end()) {
        auto& [cv, condFlg] = it->second;
        cv.notify_one();
        condFlg = true;
    }
}

bool cs::DumbCv::waitCvSignal(const cs::Signature& signature) {
    bool isTimeOver = false;
    std::unique_lock lock(mutex_);

    if (auto it = cvInfo_.find(signature); it != cvInfo_.end()) {
        isTimeOver = it->second.cv.wait_for(lock, std::chrono::seconds(kWaitTimeMs), [it]() -> bool {
            return it->second.condFlg;
        });

        cvInfo_.erase(signature);
    }

    return isTimeOver;
}
