#pragma once

#include <assert.h>
#include <lib/system/logger.hpp>
#include <client/params.hpp>

#define BOTTLENECKED_SMARTS

#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <list>

namespace Credits {

class spinlock
{
    std::atomic_flag af = ATOMIC_FLAG_INIT;

  public:
    void lock()
    {
        //TRACE("");
        while (af.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        //TRACE("");
    }

    void unlock()
    {
        //TRACE("");
        af.clear(std::memory_order_release);
    };
};

template<typename S>
struct worker_queue
{
  private:
#ifdef BOTTLENECKED_SMARTS
	typedef std::list<std::tuple<>> tids_t;
    tids_t tids;
    std::unordered_map<std::thread::id, typename tids_t::iterator> tid_map;
#endif
    std::condition_variable_any w;
    Credits::spinlock lock;
    S state;

  public:
    void get_position()
    {
#ifdef BOTTLENECKED_SMARTS
        //TRACE("");
        std::lock_guard<decltype(lock)> l(lock);
        //TRACE("");
        auto tid = std::this_thread::get_id();
        //TRACE("");
        tid_map[tid] = tids.insert(tids.end(), std::make_tuple());
        //TRACE("");
#endif
    }

    template<typename J>
    void wait_till_front(const J& j)
    {
        //TRACE("");
        std::unique_lock<decltype(lock)> l(lock);
#ifdef BOTTLENECKED_SMARTS
        //TRACE("");
        auto tid = std::this_thread::get_id();
        //TRACE("");
        auto tit = tid_map.find(tid);
        //TRACE("");
        assert(tit != tid_map.end());
        //TRACE("");
#endif
        w.wait(l, [&]() {
            //TRACE("");
            if (
#ifdef BOTTLENECKED_SMARTS
              tit->second == tids.begin() &&
#endif
              j(state)) {
#ifdef BOTTLENECKED_SMARTS
                //TRACE("");
                tids.pop_front();
                //TRACE("");
                tid_map.erase(tit);
                //TRACE("");
                w.notify_all();
                //TRACE("");
#endif
                return true;
            }
            //TRACE("");
            return false;
        });
        //TRACE("");
    }
    void yield()
    {
#ifdef BOTTLENECKED_SMARTS
        //TRACE("");
        std::unique_lock<decltype(lock)> l(lock);
        //TRACE("");
        auto tid = std::this_thread::get_id();
        //TRACE("");
        auto tit = tid_map.find(tid);
        //TRACE("");
        if (tit == tid_map.end()) {
            //TRACE("");
            return;
        }
        //TRACE("");
        
        //TRACE("");
        bool needNotifyAll = tit->second == tids.begin();
        tids.erase(tit->second);
        //TRACE("");
        tid_map.erase(tit);
        //TRACE("");       

        if (needNotifyAll) {
          //TRACE("");          
          w.notify_all();
          //TRACE("");
        }
         
#endif     
    }
    template<typename J>
    void update_state(const J& j)
    {
        //TRACE("");
        std::lock_guard<decltype(lock)> l(lock);
        //TRACE("");
        state = j();
        //TRACE("");
        w.notify_all();
        //TRACE("");
    }
};


struct sweet_spot
{
#ifdef BOTTLENECKED_SMARTS
  private:
    std::condition_variable_any cv;
    Credits::spinlock lock;
    bool occupied = false;
#endif

  public:
    void occupy()
    {
#ifdef BOTTLENECKED_SMARTS
        //TRACE("");
        std::unique_lock<decltype(lock)> l(lock);
        //TRACE("");
        cv.wait(l, [this]() {
            //TRACE("");
            auto res = !occupied;
            occupied = true;
            //TRACE(res);
            return res;
        });
#endif
    }
    void leave()
    {
#ifdef BOTTLENECKED_SMARTS
        //TRACE("");
        std::lock_guard<decltype(lock)> l(lock);
        //TRACE(occupied);
        occupied = false;
        //TRACE("");
        cv.notify_one();
        //TRACE("");
#endif
    }
};

/*

template<typename T>
struct LockedRef;

template<typename T>
struct Lockable
{
    using LockedType = T;

    template<typename... Args>
    Lockable(Args&&... args)
        : t(std::forward<Args>(args)...)
    {}

private:
    std::mutex m;
    T t;

    friend struct LockedRef<T>;
};

template<typename T>
struct LockedRef
{
private:
    Lockable<T> *l;

public:
    LockedRef(Lockable<T>& l)
        : l(&l)
    {
        this->l->m.lock();
    }
    ~LockedRef() { if (l) l->m.unlock(); }

    LockedRef(const LockedRef&) = delete;
    LockedRef(LockedRef&& other)
        : l(other.l)
    {
        other.l = nullptr;
    }

    operator T*()
    {
        return &(l->t);
    }
    T* operator->() { return *this; }
    T& operator*() { return l->t; }
};*/

template<typename T>
struct SpinLockedRef;

template<typename T>
struct SpinLockable
{
    using LockedType = T;

    template<typename... Args>
    SpinLockable(Args&&... args)
        : t(std::forward<Args>(args)...)
    {}

private:
    std::atomic_flag af = ATOMIC_FLAG_INIT;
    T t;

    friend struct SpinLockedRef<T>;
};

template<typename T>
struct SpinLockedRef
{
private:
    SpinLockable<T> *l;

public:
    SpinLockedRef(SpinLockable<T>& l)
        : l(&l)
    {
        while (this->l->af.test_and_set(std::memory_order_acquire))
            ;
    }
    ~SpinLockedRef() { if (l) l->af.clear(std::memory_order_release); }

    SpinLockedRef(const SpinLockedRef&) = delete;
    SpinLockedRef(SpinLockedRef&& other)
        : l(other.l)
    {
        other.l = nullptr;
    }

    operator T*() { return &(l->t); }
    T* operator->() { return *this; }
    T& operator*() { return l->t; }
};
/*
template<typename T>
LockedRef<T>
locked_ref(Lockable<T>& l)
{
    LockedRef<T> r(l);
    return r;
}
*/
template<typename T>
SpinLockedRef<T>
locked_ref(SpinLockable<T>& l)
{
    return SpinLockedRef<T>(l);
}

}