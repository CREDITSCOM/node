/* Send blaming letters to @yrtimd */
#ifndef PACMANS_HPP
#define PACMANS_HPP

#include <queue>
#include <mutex>
#include <atomic>
#include <boost/asio.hpp>

#include "packet.hpp"

namespace ip = boost::asio::ip;

template <typename Pacman>
class TaskPtr {
public:
  TaskPtr(TaskPtr&& rhs)
  : ptr_(rhs.ptr_)
  , owner_(rhs.owner_) {
    rhs.ptr_ = nullptr;
  }

  TaskPtr(const TaskPtr&) = delete;
  TaskPtr& operator=(const TaskPtr&) = delete;
  TaskPtr& operator=(TaskPtr&&) = delete;

  ~TaskPtr() {
    if (ptr_) {
      owner_->releaseTask(ptr_);
    }
  }

  typename Pacman::Task* operator->() {
    return ptr_;
  }
  const typename Pacman::Task* operator->() const {
    return ptr_;
  }

private:
  TaskPtr() {}

  typename Pacman::Task* ptr_;
  Pacman* owner_;

  friend Pacman;
};

class IPacMan {
public:
  IPacMan()
  : allocator_(1 << 20) {}

  struct Task {
    ip::udp::endpoint sender;
    size_t size;
    Packet pack;
  };

  Task& allocNext();
  void enQueueLast();

  TaskPtr<IPacMan> getNextTask();
  void releaseTask(Task*);

private:
  Task lastElt_;
  std::queue<Task> queue_;
  std::mutex mutex_;
  std::atomic<size_t> size_ = {0};
  RegionAllocator allocator_;
};

class OPacMan {
public:
  struct Task {
    ip::udp::endpoint endpoint;
    Packet pack;
  };

  Task* allocNext();
  void enQueueLast();

  TaskPtr<OPacMan> getNextTask();
  void releaseTask(Task*);

private:
  Task lastElt_;
  std::queue<Task> queue_;
  std::mutex mutex_;
  std::atomic<size_t> size_ = {0};
};

#endif  // PACMANS_HPP
