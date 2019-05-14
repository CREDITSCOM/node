/* Send blaming letters to @yrtimd */
#ifndef PACMANS_HPP
#define PACMANS_HPP

#include <atomic>
#include <boost/asio.hpp>
#include <list>
#include <mutex>

#include "packet.hpp"

namespace ip = boost::asio::ip;

template <typename Pacman>
class TaskPtr {
public:
    TaskPtr(TaskPtr&& rhs)
    : it_(rhs.it_)
    , owner_(rhs.owner_) {
        valid_ = true;
        rhs.valid_ = false;
    }

    TaskPtr(const TaskPtr&) = delete;
    TaskPtr& operator=(const TaskPtr&) = delete;
    TaskPtr& operator=(TaskPtr&&) = delete;

    ~TaskPtr() {
        if (valid_) {
            owner_->releaseTask(it_);
            //      valid_ = false;
        }
    }

    typename Pacman::Task* operator->() {
        return &(static_cast<typename Pacman::Task&>(*it_));
    }
    const typename Pacman::Task* operator->() const {
        return &(static_cast<typename Pacman::Task&>(*it_));
    }

private:
    TaskPtr() {
    }

    typename Pacman::TaskIterator it_;
    Pacman* owner_;
    bool valid_ = true;

    friend Pacman;
};

template <typename Task>
struct TaskBody {
    operator Task&() {
        return *reinterpret_cast<Task*>(data);
    }

    char data[sizeof(Task)];
};

class IPacMan {
public:
    IPacMan()
    : allocator_(1 << 20) {
    }

    struct Task {
        ip::udp::endpoint sender;
        size_t size;
        Packet pack;
    };

    Task& allocNext();
    void enQueueLast();

    TaskPtr<IPacMan> getNextTask();

    using TaskIterator = std::list<TaskBody<Task>>::iterator;
    void releaseTask(TaskIterator&);
    void rejectLast();

private:
    std::list<TaskBody<Task>> queue_;
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

    using TaskIterator = std::list<TaskBody<Task>>::iterator;
    void releaseTask(TaskIterator&);

private:
    std::list<TaskBody<Task>> queue_;
    std::mutex mutex_;
    std::atomic<size_t> size_ = {0};
};

#endif  // PACMANS_HPP
