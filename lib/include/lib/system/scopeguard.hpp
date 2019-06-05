#ifndef SCOPE_GUARD_HPP
#define SCOPE_GUARD_HPP

#include <utility>
#include <lib/system/logger.hpp>

namespace cs {
struct ScopeGuardBase {
    ScopeGuardBase()
    : isActive_(true) {
    }

    ScopeGuardBase(ScopeGuardBase&& rhs)
    : isActive_(rhs.isActive_) {
        rhs.dismiss();
    }

    void dismiss() noexcept {
        isActive_ = false;
    }

protected:
    ~ScopeGuardBase() = default;
    bool isActive_;
};

template <class Func>
struct ScopeGuard : public ScopeGuardBase {
    ScopeGuard() = delete;
    ScopeGuard(const ScopeGuard&) = delete;

    ScopeGuard(Func f) noexcept
    : ScopeGuardBase()
    , func_(std::move(f)) {
    }

    ScopeGuard(ScopeGuard&& rhs) noexcept
    : ScopeGuardBase(std::move(rhs))
    , func_(std::move(rhs.func_)) {
    }

    ~ScopeGuard() noexcept {
        if (isActive_) {
            try {
                func_();
            }
            catch (...) {
                cserror() << "Scope guard callable exception, " << typeid(Func).name() << " func type";
            }
        }
    }

    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    Func func_;
};

template <class Fun>
ScopeGuard<Fun> scopeGuard(Fun f) {
    return ScopeGuard<Fun>(std::move(f));
}
}  // namespace cs

#endif
