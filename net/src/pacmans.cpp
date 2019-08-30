/* Send blaming letters to @yrtimd */
#include "pacmans.hpp"

IPacMan::Task& IPacMan::allocNext() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.emplace_back();
    auto end = queue_.end();
    Task& task = *(--end);
    task.pack.region_ = allocator_.allocateNext(Packet::MaxSize);
    return task;
}

void IPacMan::enQueueLast() {
    Task& task = queue_.back();
    task.pack.setSize(static_cast<uint32_t>(task.size));

    size_.fetch_add(1, std::memory_order_acq_rel);
	//if (size_ > 500) {
	//	csinfo() << __func__ << ": IPackMan queue size = " << queue_.size();
	//}
}

void IPacMan::rejectLast() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.pop_back();
}

TaskPtr<IPacMan> IPacMan::getNextTask(bool &is_empty) {
    TaskPtr<IPacMan> result;
    if (!size_.load(std::memory_order_acquire)) {
        is_empty = true;
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    result.owner_ = this;
    result.it_ = queue_.begin();
	//if (size_ > 500) {
	//	csinfo() << __func__ << ": IPackMan queue size = " << queue_.size();
	//}
    return result;
}

void IPacMan::releaseTask(TaskIterator& it) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.erase(it);
    size_.fetch_sub(1, std::memory_order_acq_rel);
	//if (size_ > 500) {
	//	csinfo() << __func__ << ": IPackMan queue size = " << queue_.size();
	//}
}

OPacMan::Task* OPacMan::allocNext() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.emplace_back();
    auto end = queue_.end();
    Task& task = *(--end);
    return &task;
}

void OPacMan::enQueueLast() {
    size_.fetch_add(1, std::memory_order_acq_rel);
	//if (size_ > 500) {
	//	csinfo() << __func__ << ": OPackMan queue size = " << queue_.size();
	//}
}

TaskPtr<OPacMan> OPacMan::getNextTask(bool &is_empty) {
    TaskPtr<OPacMan> result;
    if (!size_.load(std::memory_order_acquire)) {
        is_empty = true;
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    result.owner_ = this;
    result.it_ = queue_.begin();
	//if (size_ > 500) {
	//	csinfo() << __func__ << ": OPackMan queue size = " << queue_.size();
	//}
    return result;
}

void OPacMan::releaseTask(TaskIterator& it) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.erase(it);
    size_.fetch_sub(1, std::memory_order_acq_rel);
	//if (size_ > 500) {
	//	csinfo() << __func__ << ": OPackMan queue size = " << queue_.size();
	//}
}
