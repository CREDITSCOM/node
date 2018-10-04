/* Send blaming letters to @yrtimd */
#ifndef __PACMANS_HPP__
#define __PACMANS_HPP__
#include <boost/asio.hpp>

#include <lib/system/queues.hpp>

#include "packet.hpp"

using namespace boost::asio;

template <typename Pacman>
class TaskPtr {
public:
  TaskPtr(TaskPtr&& rhs): ptr_(rhs.ptr_),
                          owner_(rhs.owner_) {
    rhs.ptr_ = nullptr;
  }

  TaskPtr(const TaskPtr&) = delete;
  TaskPtr& operator=(const TaskPtr&) = delete;
  TaskPtr& operator=(TaskPtr&&) = delete;

  ~TaskPtr() {
    if (ptr_) owner_->releaseTask(ptr_);
  }

  typename Pacman::Task* operator->() { return &(ptr_->element); }
  const typename Pacman::Task* operator->() const { return &(ptr_->element); }

private:
  TaskPtr() { }

  typename Pacman::Queue::Element* ptr_;
  Pacman* owner_;

  friend Pacman;
};

class IPacMan {
public:
  IPacMan(): allocator_(1 << 20) { }

  struct Task {
    ip::udp::endpoint sender;
    size_t size;
    Packet pack;

    Task() { }

    Task(const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator=(const Task&) = delete;
    Task& operator=(Task&&) = delete;
  };

  typedef FUQueue<Task, 1000000lu> Queue;

  Task& allocNext();
  void enQueueLast();

  TaskPtr<IPacMan> getNextTask();
  void releaseTask(Queue::Element*);

private:
  Queue queue_;
  typename Queue::Element* lastElt_;
  bool lockedLast_ = false;

  RegionAllocator allocator_;

};

class OPacMan {
public:
  const static std::size_t MaxTimesRedirect = 1;

  struct Task {
    ip::udp::endpoint endpoint;
    Packet pack;

    Task() { }

    Task(const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator=(const Task&) = delete;
    Task& operator=(Task&&) = delete;
  };

  typedef FUQueue<Task, 1000000lu> Queue;

  Queue::Element* allocNext();
  void enQueueLast(Queue::Element*);

  TaskPtr<OPacMan> getNextTask();
  void releaseTask(Queue::Element*);

private:
  Queue queue_;
};

#endif // __PACMANS_HPP__
