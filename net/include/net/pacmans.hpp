/* Send blaming letters to @yrtimd */
#ifndef PACMANS_HPP
#define PACMANS_HPP

#include <boost/asio.hpp>
#include <boost/lockfree/spsc_queue.hpp>

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

  using Queue = boost::lockfree::spsc_queue<Task, boost::lockfree::capacity<1024>>;

  Task& allocNext();
  void enQueueLast();

  TaskPtr<IPacMan> getNextTask();
  void releaseTask(Task*);

private:
  Task lastElt_;
  Queue queue_;
  RegionAllocator allocator_;
};

class OPacMan {
public:
  struct Task {
    ip::udp::endpoint endpoint;
    Packet pack;
  };

  using Queue = boost::lockfree::spsc_queue<Task, boost::lockfree::capacity<1024>>;

  Task* allocNext();
  void enQueueLast();

  TaskPtr<OPacMan> getNextTask();
  void releaseTask(Task*);

private:
  Task lastElt_;
  Queue queue_;
};

#endif  // PACMANS_HPP
