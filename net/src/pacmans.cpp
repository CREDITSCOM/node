/* Send blaming letters to @yrtimd */
#include "pacmans.hpp"

IPacMan::Task& IPacMan::allocNext() {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.emplace_back();
  auto end = queue_.end();
  Task& task = *(--end);
  new (&task) Task();
  task.pack.data_ = allocator_.allocateNext(Packet::MaxSize);
  return task;
}

void IPacMan::enQueueLast() {
  Task& task = queue_.back();
  allocator_.shrinkLast(static_cast<uint32_t>(task.size));
  size_.fetch_add(1, std::memory_order_acq_rel);
}

void IPacMan::rejectLast() {
  std::lock_guard<std::mutex> lock(mutex_);
  Task &task = queue_.back();
  task.~Task();
  queue_.pop_back();
}

TaskPtr<IPacMan> IPacMan::getNextTask() {
  while (!size_.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  TaskPtr<IPacMan> result;
  result.owner_ = this;
  result.it_ = queue_.begin();
  return result;
}

void IPacMan::releaseTask(TaskIterator& it) {
  Task &task = *it;
  task.~Task();
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.erase(it);
  size_.fetch_sub(1, std::memory_order_acq_rel);
}

OPacMan::Task* OPacMan::allocNext() {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.emplace_back();
  auto end = queue_.end();
  Task& task = *(--end);
  new (&task) Task();
  return &task;
}

void OPacMan::enQueueLast() {
  size_.fetch_add(1, std::memory_order_acq_rel);
}

TaskPtr<OPacMan> OPacMan::getNextTask() {
  while (!size_.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  TaskPtr<OPacMan> result;
  result.owner_ = this;
  result.it_ = queue_.begin();
  return result;
}

void OPacMan::releaseTask(TaskIterator& it) {
  Task &task = *it;
  task.~Task();
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.erase(it);
  size_.fetch_sub(1, std::memory_order_acq_rel);
}
