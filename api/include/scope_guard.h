#pragma once

struct ScopeGuardBase
{
    ScopeGuardBase()
      : mActive(true)
    {}

    ScopeGuardBase(ScopeGuardBase&& rhs)
      : mActive(rhs.mActive)
    {
        rhs.dismiss();
    }

    void dismiss() noexcept { mActive = false; }

  protected:
    ~ScopeGuardBase() = default;
    bool mActive;
};

template<class Fun>
struct ScopeGuard : public ScopeGuardBase
{
    ScopeGuard() = delete;
    ScopeGuard(const ScopeGuard&) = delete;

    ScopeGuard(Fun f) noexcept
      : ScopeGuardBase()
      , mF(std::move(f))
    {}

    ScopeGuard(ScopeGuard&& rhs) noexcept
      : ScopeGuardBase(std::move(rhs))
      , mF(std::move(rhs.mF))
    {}

    ~ScopeGuard() noexcept
    {
        if (mActive) {
            try {
                mF();
            } catch (...) {
            }
        }
    }

    ScopeGuard& operator=(const ScopeGuard&) = delete;

  private:
    Fun mF;
};

template<class Fun>
ScopeGuard<Fun>
scopeGuard(Fun f)
{
    return ScopeGuard<Fun>(std::move(f));
}
