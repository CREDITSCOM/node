#ifndef CONCURRENT_HPP
#define CONCURRENT_HPP

#include <assert.h>
#include <condition_variable>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include <lib/system/cache.hpp>
#include <lib/system/common.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/signals.hpp>
#include <lib/system/structures.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>

namespace cs {
enum class RunPolicy : cs::Byte {
    CallQueuePolicy,
    ThreadPolicy
};

enum class WatcherState : cs::Byte {
    Idle,
    Running,
    Compeleted
};

// aliasing
using Threads = boost::asio::thread_pool;

// returns instance of boost thread pool
class ThreadPool {
public:
    ThreadPool() = delete;
    ~ThreadPool() = default;

    static Threads& instance() noexcept {
        static Threads threadPool(std::thread::hardware_concurrency());
        return threadPool;
    }
};

template <typename T>
class FutureWatcher;
class Concurrent;

// concurrent private helper
class Worker {
private:
    template <typename Func>
    static void execute(cs::RunPolicy policy, Func&& function) {
        if (policy == cs::RunPolicy::CallQueuePolicy) {
            CallsQueue::instance().insert(std::forward<Func>(function));
        }
        else {
            function();
        }
    }

    template <typename Func>
    static void execute(Func&& function) {
        auto& threadPool = ThreadPool::instance();
        boost::asio::post(threadPool, std::forward<Func>(function));
    }

    template <typename Func>
    static void run(Func&& function) {
        try {
            std::thread thread(std::forward<Func>(function));
            thread.detach();
        }
        catch (...) {
            execute(std::forward<Func>(function));
        }
    }

    template <typename T>
    friend class FutureBase;

    template <typename T>
    friend class FutureWatcher;
    friend class Concurrent;
};

// future entity
template <typename T>
using Future = std::future<T>;

template <typename Result>
class FutureBase {
    friend class Concurrent;

public:
    using Id = uint64_t;

protected:
    FutureBase() {
        ++producedId;

        id_ = producedId;
        state_ = WatcherState::Idle;
        policy_ = RunPolicy::ThreadPolicy;
    }

    FutureBase(FutureBase&) = delete;
    ~FutureBase() = default;

    explicit FutureBase(const RunPolicy policy, Future<Result>&& future)
    : FutureBase() {
        future_ = std::move(future);
        policy_ = policy;
    }

    FutureBase(FutureBase&& watcher) noexcept
    : future_(std::move(watcher.future_))
    , policy_(watcher.policy_)
    , state_(watcher.state_)
    , id_(watcher.id_) {
    }

    FutureBase& operator=(FutureBase&& watcher) noexcept {
        if (state_ == WatcherState::Running) {
            cserror() << csname() << "Trying to use operator= in watcher running state";
        }

        future_ = std::move(watcher.future_);
        policy_ = watcher.policy_;
        id_ = watcher.id_;

        return *this;
    }

public:
    // returns current watcher state, if watcher never watched runnable entity
    // then his state is Idle
    WatcherState state() const noexcept {
        return state_;
    }

    Id id() const noexcept {
        return id_;
    }

protected:
    using CompletedSignal = cs::Signal<void(Id)>;

    Future<Result> future_;
    RunPolicy policy_;
    WatcherState state_ = WatcherState::Idle;
    Id id_;

    inline static Id producedId = 0;
    constexpr static std::chrono::milliseconds kAwaiterTime{10};

    void setCompletedState() {
        future_ = Future<Result>();
        state_ = WatcherState::Compeleted;

        emit completed(id_);
    }

    template <typename Func>
    void callSignal(Func&& func) {
        Worker::execute(policy_, std::forward<Func>(func));
    }

    template <typename Awaiter>
    void await(const Awaiter& awaiter) {
        if (cs::Connector::callbacks(&awaiter) != 0) {
            return;
        }

        std::this_thread::sleep_for(kAwaiterTime);
    }

protected signals:

    // internal utility signal, clients should used finished/failed signlas from FutureWatcher
    CompletedSignal completed;
};

// object to get future result from concurrent
// and generate signal when finished
template <typename Result>
class FutureWatcher : public FutureBase<Result> {
public:
    using FinishSignal = cs::Signal<void(const Result&)>;
    using FailedSignal = cs::Signal<void()>;

    explicit FutureWatcher(RunPolicy policy, Future<Result>&& future)
    : FutureBase<Result>(policy, std::move(future)) {
        watch();
    }

    FutureWatcher() = default;
    ~FutureWatcher() = default;
    FutureWatcher(FutureWatcher&&) = default;

    FutureWatcher& operator=(FutureWatcher&& watcher) {
        FutureBase<Result>::operator=(std::move(watcher));

        watch();
        return *this;
    }

protected:
    using Super = FutureBase<Result>;

    void watch() {
        auto closure = [=] {
            try {
                Result result = Super::future_.get();

                auto lambda = [this, res = std::move(result)]() {
                    Super::await(finished);
                    emit finished(std::move(res));

                    Super::setCompletedState();
                };

                Super::callSignal(std::bind(std::move(lambda)));
            }
            catch (std::exception& e) {
                Super::await(failed);

                cserror() << "Concurrent execution with " << typeid(Result).name() << " failed, " << e.what();
                emit failed();
            }
        };

        Super::state_ = WatcherState::Running;
        Worker::execute(std::move(closure));
    }

public signals:
    FinishSignal finished;
    FailedSignal failed;
};

template <>
class FutureWatcher<void> : public FutureBase<void> {
public:
    using FinishSignal = cs::Signal<void()>;
    using FailedSignal = cs::Signal<void()>;

    explicit FutureWatcher(RunPolicy policy, Future<void>&& future)
    : FutureBase<void>(policy, std::move(future)) {
        watch();
    }

    FutureWatcher() = default;
    ~FutureWatcher() = default;
    FutureWatcher(FutureWatcher&& watcher) = default;

    FutureWatcher& operator=(FutureWatcher&& watcher) noexcept {
        FutureBase<void>::operator=(std::move(watcher));

        watch();
        return *this;
    }

protected:
    using Super = FutureBase<void>;

    void watch() {
        auto closure = [=] {
            try {
                Super::future_.get();

                auto signal = [=] {
                    Super::await(finished);
                    emit finished();

                    Super::setCompletedState();
                };

                Super::callSignal(std::move(signal));
            }
            catch (std::exception& e) {
                Super::await(failed);

                cserror() << "Concurrent execution with void result failed, " << e.what();
                emit failed();
            }
        };

        Super::state_ = WatcherState::Running;
        Worker::execute(std::move(closure));
    }

public signals:
    FinishSignal finished;
    FailedSignal failed;
};

// safe pointer to watcher
template <typename T>
using FutureWatcherPtr = std::shared_ptr<FutureWatcher<T>>;

class Concurrent {
    template<typename T>
    using Executions = std::list<T>;

public:
    // runs function in another thread, returns future watcher
    // than generates finished signal by run policy
    // you should not store watcher, it does by run method, just use finished/failed signal to subscribe
    template <typename Func, typename... Args>
    static FutureWatcherPtr<std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>> run(RunPolicy policy, Func&& function, Args&&... args) {
        using ReturnType = std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>;
        using WatcherType = FutureWatcher<ReturnType>;

        // running executions
        static Executions<FutureWatcherPtr<ReturnType>> executions;
        using ExecutionsIterator = typename decltype(executions)::iterator;

        // run async and store it at executions
        auto watcher = std::make_shared<WatcherType>(policy, std::async(std::launch::async, std::forward<Func>(function), std::forward<Args>(args)...));

        {
            cs::Lock lock(executionsMutex_);
            executions.push_back(watcher);
        }

        // watcher will be removed after lambda called
        cs::Connector::connect(&watcher->completed, [](typename FutureWatcher<ReturnType>::Id id) {
            ExecutionsIterator iter;

            {
                cs::Lock lock(executionsMutex_);
                iter = std::find_if(executions.begin(), executions.end(), [=](const auto& watcher) {
                    return (watcher->id() == id) && (watcher->state() == WatcherState::Compeleted);
                });
            }

            if (iter != executions.end()) {
                cs::Concurrent::run([=]() {
                    cs::Lock lock(executionsMutex_);
                    executions.erase(iter);
                });
            }
        });

        return watcher;
    }

    // runs function entity in thread pool
    template <typename Func>
    static void run(Func&& function) {
        Worker::execute(std::forward<Func>(function));
    }

    // runs non-binded function in thread pool
    template <typename Func, typename... Args>
    static void run(Func&& func, Args&&... args) {
        Concurrent::run(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
    }

    // calls std::function after ms time in another thread
    static void runAfter(const std::chrono::milliseconds& ms, cs::RunPolicy policy, std::function<void()> callBack) {
        auto timePoint = std::chrono::steady_clock::now() + ms;
        Worker::run(std::bind(&Concurrent::runAfterHelper, timePoint, policy, std::move(callBack)));
    }

    template <typename Func>
    static void execute(cs::RunPolicy policy, Func&& function) {
        Worker::execute(policy, std::forward<Func>(function));
    }

private:
    static void runAfterHelper(const std::chrono::steady_clock::time_point& timePoint, cs::RunPolicy policy, std::function<void()> callBack) {
        std::this_thread::sleep_until(timePoint);
        Worker::execute(policy, std::move(callBack));
    }

    inline static std::mutex executionsMutex_;
};

template <typename T>
inline bool operator<(const FutureWatcher<T>& lhs, const FutureWatcher<T>& rhs) {
    return lhs.id() < rhs.id();
}

template <typename T>
inline bool operator==(const FutureWatcher<T>& watcher, uint64_t value) {
    return watcher.id() == value;
}

// for api threading
template <typename S>
struct WorkerQueue {
private:
    using tids_t = std::list<std::tuple<>>;
    tids_t tids_;
    std::unordered_map<std::thread::id, typename tids_t::iterator> tidMap_;

    std::condition_variable_any conditionalVariable_;
    cs::SpinLock lock_{ATOMIC_FLAG_INIT};
    S hash_;

    const unsigned int kWaitSecondsTime{ 30 };

public:
    inline WorkerQueue() noexcept
    : lock_() {
    }

    void getPosition() {
        cs::Lock lock(lock_);
        auto tid = std::this_thread::get_id();
        tidMap_[tid] = tids_.insert(tids_.end(), std::make_tuple());
    }

    template <typename T>
    bool waitTillFront(const T& type) {
        std::unique_lock lock(lock_);
        auto res = conditionalVariable_.wait_for(lock, std::chrono::seconds(kWaitSecondsTime), [&]() { return type(hash_); });
        hash_.condFlg = false;
        tidMap_.erase(std::this_thread::get_id());
        conditionalVariable_.notify_all();
        return res;
    }

    void yield() {
        std::unique_lock lock(lock_);

        auto tid = std::this_thread::get_id();
        auto tit = tidMap_.find(tid);

        if (tit == tidMap_.end()) {
            return;
        }

        bool needNotifyAll = tit->second == tids_.begin();
        tids_.erase(tit->second);

        tidMap_.erase(tit);

        if (needNotifyAll) {
            conditionalVariable_.notify_all();
        }
    }

    template <typename Hash>
    void updateHash(const Hash& hash) {
        cs::Lock lock(lock_);
        hash_ = hash(hash_);
        conditionalVariable_.notify_all();
    }
};

// what is sweet spot?
struct SweetSpot {
private:
    std::condition_variable_any conditionalVariable_;
    cs::SpinLock lock_{ATOMIC_FLAG_INIT};
    bool occupied_ = false;

public:
    inline SweetSpot() noexcept
    : lock_() {
    }

    void occupy() {
        std::unique_lock lock(lock_);

        conditionalVariable_.wait(lock, [this]() {
            auto res = !occupied_;
            occupied_ = true;
            return res;
        });
    }

    void leave() {
        cs::Lock lock(lock_);
        occupied_ = false;
        conditionalVariable_.notify_one();
    }
};

template <typename T>
struct SpinLockedRef;

template <typename T>
struct SpinLockable {
    using LockedType = T;

    template <typename... Args>
    SpinLockable(Args&&... args)
    : type_(std::forward<Args>(args)...) {
    }

    void lock() {
        mutex_.lock();
    }

    void unlock() {
        mutex_.unlock();
    }

    void erase(const cs::Signature& elm) {
        type_.erase(elm);
    }

private:
    std::mutex mutex_;
    T type_;
#ifdef API_SPINLOCK
    __cacheline_aligned std::atomic_flag atomicFlag_ = ATOMIC_FLAG_INIT;
#endif

    friend struct SpinLockedRef<T>;
};

template <typename T>
struct SpinLockedRef {
private:
    SpinLockable<T>* lockable_;
public:
    SpinLockedRef(SpinLockable<T>& lockable)
    : lockable_(&lockable) {
#ifdef API_SPINLOCK
        while (this->lockable_->atomicFlag_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
#else
        lockable_->lock();
#endif
    }

    ~SpinLockedRef() {
#ifdef API_SPINLOCK
        if (lockable_) {
            lockable_->atomicFlag_.clear(std::memory_order_release);
        }
#else
        lockable_->unlock();
#endif
    }

    SpinLockedRef(const SpinLockedRef&) = delete;
    SpinLockedRef(SpinLockedRef&& other)
    : lockable_(other.lockable_) {
        other.lockable_ = nullptr;
    }

    explicit operator T*() {
        return &(lockable_->type_);
    }

    T* operator->() {
        return static_cast<T*>(*this);
    }

    T& operator*() {
        return lockable_->type_;
    }
};

template <typename T>
SpinLockedRef<T> lockedReference(SpinLockable<T>& lockable) {
    return SpinLockedRef<T>(lockable);
}
}  // namespace cs
#endif  //  CONCURRENT_HPP
