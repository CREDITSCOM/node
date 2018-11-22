/* Send blaming letters to @yrtimd */
#ifndef QUEUES_HPP
#define QUEUES_HPP
#include <atomic>
#include <cstdint>
#include <thread>

#include "cache.hpp"
#include "logger.hpp"

/* Fixed Uniform Queue (FUQueue) is a simple lock-free queue that
   allows many writers and many readers */
template <typename T, std::size_t MaxSize, uint32_t BackOffTreshold = 1000>
class FUQueue {
public:
  struct Element {
    enum class State : uint8_t
    {
      Empty,
      New,
      Read,
      Write
    };

    __cacheline_aligned std::atomic<State> lockState{State::Empty};
    T element;
  };

  Element* lockRead() {
    return lock(/*[*/ readingBarrier_, writingBarrier_ /*]*/,
                /*if*/ Element::State::New,
                /*then set to*/ Element::State::Read,
                /*or spin while*/ Element::State::Write);
  }

  void unlockRead(Element* ptr) {
    unlock(ptr,
           /*change from*/ Element::State::Read,
           /*to*/ Element::State::Empty,
           /*and move*/ readingBarrier_);
  }

  Element* lockWrite() {
    return lock(/*[*/ writingBarrier_, readingBarrier_ /*]*/,
                /*if*/ Element::State::Empty,
                /*then set to*/ Element::State::Write,
                /*or spin while*/ Element::State::Read);
  }

  void unlockWrite(Element* ptr) {
    unlock(ptr,
           /*change from*/ Element::State::Write,
           /*to*/ Element::State::New,
           /*and move*/ readingBarrier_);
  }

private:
  Element* nextPtr(Element* ptr) {
    if (++ptr == end)
      ptr = elements;

    return ptr;
  }

  Element* lock(std::atomic<Element*>& leftBarr, std::atomic<Element*>& rightBarr, typename Element::State targetState,
                typename Element::State newState, typename Element::State spinState) {
    auto target = leftBarr.load(std::memory_order_acquire);
    typename Element::State state = targetState;

    uint32_t attempts = 0;
    while (!target->lockState.compare_exchange_strong(state, newState, std::memory_order_release,
                                                      std::memory_order_relaxed)) {
      if (++attempts == BackOffTreshold) {
        attempts = 0;
        std::this_thread::yield();
      }

      if (state != spinState && target != rightBarr.load(std::memory_order_relaxed)) {
        auto nextTarget = nextPtr(target);
        leftBarr.compare_exchange_strong(target, nextPtr(target), std::memory_order_release, std::memory_order_relaxed);
        target = nextTarget;
      }

      state = targetState;
    }

    return target;
  }

  void unlock(Element* ptr, typename Element::State oldState, typename Element::State newState,
              std::atomic<Element*>& movingBar) {
    if (!ptr->lockState.compare_exchange_strong(oldState, newState, std::memory_order_release,
                                                std::memory_order_relaxed)) {
      cserror() << "Unexpected element state";
    }

    movingBar.compare_exchange_strong(ptr, nextPtr(ptr), std::memory_order_release, std::memory_order_relaxed);
  }

  Element elements[MaxSize];
  Element* end = elements + MaxSize;

  __cacheline_aligned std::atomic<Element*> readingBarrier_ = {elements};
  __cacheline_aligned std::atomic<Element*> writingBarrier_ = {elements};
};

#endif  // QUEUES_HPP
