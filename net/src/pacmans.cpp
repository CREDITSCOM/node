/* Send blaming letters to @yrtimd */
#include "pacmans.hpp"

IPacMan::Task& IPacMan::allocNext() {
  new (&lastElt_) Task();
  lastElt_.pack.data_ = allocator_.allocateNext(Packet::MaxSize);
  return lastElt_;
}

void IPacMan::enQueueLast() {
  std::lock_guard<std::mutex> lock(mutex_);
  allocator_.shrinkLast(static_cast<uint32_t>(lastElt_.size));
  queue_.emplace(lastElt_);
  size_.fetch_add(1, std::memory_order_release);
  std::atomic_thread_fence(std::memory_order_acquire);
  lastElt_.~Task();
}

TaskPtr<IPacMan> IPacMan::getNextTask() {
  while (!size_.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  TaskPtr<IPacMan> result;
  result.owner_ = this;
  Task *elt = new Task(queue_.front());
  queue_.pop();
  size_.fetch_sub(1, std::memory_order_release);
  result.ptr_ = elt;
  return result;
}

void IPacMan::releaseTask(Task* elt) {
  delete elt;
}

OPacMan::Task* OPacMan::allocNext() {
  new (&lastElt_) Task();
  return &lastElt_;
}

void OPacMan::enQueueLast() {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.emplace(lastElt_);
  size_.fetch_add(1, std::memory_order_release);
  std::atomic_thread_fence(std::memory_order_acquire);
  lastElt_.~Task();
}

TaskPtr<OPacMan> OPacMan::getNextTask() {
  while (!size_.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  TaskPtr<OPacMan> result;
  result.owner_ = this;
  Task *elt = new Task(queue_.front());
  queue_.pop();
  size_.fetch_sub(1, std::memory_order_release);
  result.ptr_ = elt;
  return result;
}

void OPacMan::releaseTask(Task* elt) {
  delete elt;
}
