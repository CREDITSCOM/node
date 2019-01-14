#ifndef SIGNALS_HPP
#define SIGNALS_HPP

#include <lib/system/utils.hpp>

#include <functional>
#include <vector>

#define signals
#define slots
#define emit

namespace cs {
class Connector;

///
/// Base preudo signal
///
template <typename T>
class Signal;

///
/// Signal needed specialization
///
template <typename Return, typename... InArgs>
class Signal<Return(InArgs...)> {
public:
  using Argument = std::function<Return(InArgs...)>;
  using Signature = Return(InArgs...);
  using ObjectPointer = void*;
  using Slots = std::vector<std::pair<ObjectPointer, Argument>>;

  ///
  /// @brief Generates signal.
  /// @param args Any count of template parameters.
  ///
  template <typename... Args>
  inline void operator()(Args&&... args) const {
    for ([[maybe_unused]] auto& [obj, func] : slots_) {
      csunused(obj);
      if (func) {
        func(std::forward<Args>(args)...);
      }
    }
  }

  Signal() = default;
  Signal(const Signal&) = delete;
  Signal& operator=(const Signal&) = delete;

  Signal(Signal&& signal) noexcept
  : slots_(std::move(signal.slots_)) {
    signal.slots_.clear();
  }

  Signal& operator=(Signal&& signal) noexcept {
    slots_ = std::move(signal.slots_);
    signal.slots_.clear();

    return *this;
  }

  ~Signal() {
    (*this) = nullptr;
  }

private:
  // adds slot to signal
  template <typename T>
  auto& add(T&& s, ObjectPointer obj = nullptr) {
    Argument arg = std::forward<T>(s);

    if (!arg) {
      return *this;
    }

    slots_.push_back(std::make_pair(obj, std::move(arg)));
    return *this;
  }

  // clears all signal slots
  auto& operator=(void* ptr) {
    if (ptr == nullptr) {
      slots_.clear();
    }

    return *this;
  }

  std::size_t size() const noexcept {
    return slots_.size();
  }

  Slots& content() noexcept {
    return slots_;
  }

  const Slots& content() const noexcept {
    return slots_;
  }

  // all connected slots
  Slots slots_;

  friend class Connector;

  template <typename T>
  friend class Signal;
};

///
/// Signal for function prototype
///
template <typename T>
class Signal<std::function<T>> {
public:
  using Argument = std::function<T>;
  using Signature = T;
  using ObjectPointer = void*;
  using Slots = std::vector<std::pair<ObjectPointer, Argument>>;

  ///
  /// @brief Generates signal.
  /// @param args Any count of template parameters.
  ///
  template <typename... Args>
  inline void operator()(Args&&... args) const {
    signal_(std::forward<Args>(args)...);
  }

  // creation
  Signal() = default;
  Signal(const Signal&) = delete;
  Signal& operator=(const Signal&) = delete;

  Signal(Signal&& signal) noexcept
  : signal_(std::move(signal.signal_)) {
  }

  Signal& operator=(Signal&& signal) noexcept {
    signal_ = std::move(signal.signal_);
    return *this;
  }

  ~Signal() {
    (*this) = nullptr;
  }

private:
  // adds slot to signal
  template <typename U>
  auto& add(U&& s, ObjectPointer obj = nullptr) {
    signal_.add(std::forward<U>(s), obj);
    return *this;
  }

  // clears all slots
  auto& operator=(void* ptr) {
    signal_ = ptr;
    return *this;
  }

  std::size_t size() const noexcept {
    return signal_.size();
  }

  Slots& content() noexcept {
    return signal_.content();
  }

  const Slots& content() const noexcept {
    return signal_.content();
  }

  Signal<Signature> signal_;
  friend class Connector;
};

// helper namespace
namespace Args {
template <typename T>
struct GetArguments : GetArguments<decltype(&T::operator())> {};

template <typename T, typename... Args>
struct GetArguments<T (*)(Args...)> : std::integral_constant<unsigned, sizeof...(Args)> {};

template <typename T, typename C, typename... Args>
struct GetArguments<T (C::*)(Args...)> : std::integral_constant<unsigned, sizeof...(Args)> {};

template <typename T, typename C, typename... Args>
struct GetArguments<T (C::*)(Args...) const> : std::integral_constant<unsigned, sizeof...(Args)> {};

// bindings
template <int>
class CheckArgs {
public:
  template <typename T, typename Slot>
  void connect(const T& slotObj, Slot&& slot);
};

template <>
class CheckArgs<0> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj);
  }
};

template <>
class CheckArgs<1> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1);
  }
};

template <>
class CheckArgs<2> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2);
  }
};

template <>
class CheckArgs<3> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2,
                     std::placeholders::_3);
  }
};

template <>
class CheckArgs<4> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2,
                     std::placeholders::_3, std::placeholders::_4);
  }
};

template <>
class CheckArgs<5> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2,
                     std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);
  }
};

template <>
class CheckArgs<6> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2,
                     std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6);
  }
};

template <>
class CheckArgs<7> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2,
                     std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6,
                     std::placeholders::_7);
  }
};

template <>
class CheckArgs<8> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2,
                     std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6,
                     std::placeholders::_7, std::placeholders::_8);
  }
};

template <>
class CheckArgs<9> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2,
                     std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6,
                     std::placeholders::_7, std::placeholders::_8, std::placeholders::_9);
  }
};

template <>
class CheckArgs<10> {
public:
  template <typename T, typename Slot>
  auto connect(const T& slotObj, Slot&& slot) {
    return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2,
                     std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6,
                     std::placeholders::_7, std::placeholders::_8, std::placeholders::_9, std::placeholders::_10);
  }
};
}  // namespace Args

///
/// Signal - slot connection entity
///
class Connector {
public:
  explicit Connector() = delete;
  Connector(const Connector&) = delete;
  Connector& operator=(const Connector&) = delete;
  Connector(Connector&&) = delete;
  Connector& operator=(Connector&&) = delete;

  ~Connector() = default;

  ///
  /// @brief Connects signal pointer with lambda or function.
  /// @param signal Any signal pointer.
  /// @param slot Function or lambda/closure.
  ///
  template <template <typename> typename Signal, typename T>
  static void connect(const Signal<T>* signal, typename Signal<T>::Argument slot) {
    const_cast<Signal<T>*>(signal)->add(slot);
  }

  ///
  /// @brief Connects const signal pointer with objects method.
  /// @param signal Any const signal pointer.
  /// @param slotObj Pointer to slot object.
  /// @param slot Method pointer.
  ///
  template <template <typename> typename Signal, typename T, typename Object, typename Slot>
  static void connect(const Signal<T>* signal, const Object& slotObj, Slot&& slot) {
    using ObjectPointer = void*;
    constexpr int size = Args::GetArguments<Slot>();
    auto obj = reinterpret_cast<ObjectPointer>(const_cast<Object&>(slotObj));
    const_cast<Signal<T>*>(signal)->add(Args::CheckArgs<size>().connect(slotObj, std::forward<Slot>(slot)), obj);
  }

  ///
  /// @brief Connects two signals with earch other.
  /// @param lhs Any const signal1 pointer.
  /// @param rhs Any const signal2 pointer.
  ///
  template <template <typename> typename Signal, typename T>
  inline static void connect(const Signal<T>* lhs, const Signal<T>* rhs) {
    if (lhs == rhs) {
      return;
    }

    auto closure = [=](auto... args) -> void {
      if (rhs) {
        (*rhs)(std::forward<decltype(args)>(args)...)
      }
    };

    std::function<typename Signal<T>::Signature> func = closure;
    cs::Connector::connect(lhs, std::move(func));
  }

  template <template <typename> typename Signal, typename T, typename Object, typename Slot>
  static bool disconnect(const Signal<T>* signal, const Object& slotObj, Slot&& slot) {
    if (!slotObj) {
      return false;
    }

    constexpr int size = Args::GetArguments<Slot>();
    std::function<T> binder = Args::CheckArgs<size>().connect(slotObj, std::forward<Slot>(slot));

    auto& content = const_cast<Signal<T>*>(signal)->content();
    auto iterator = std::find_if(content.begin(), content.end(), [&](const auto& pair) {
      auto& [object, function] = pair;
      auto result = function.target_type().hash_code() == binder.target_type().hash_code();

      if (object) {
        if (object != (slotObj)) {
          return false;
        }
      }

      return result;
    });

    if (iterator != content.end()) {
      content.erase(iterator);
      return true;
    }

    return false;
  }

  ///
  /// @brief Drops all signal connections.
  /// @param signal Any signal object.
  ///
  template <template <typename> typename Signal, typename T>
  static bool disconnect(const Signal<T>* signal) {
    auto signalPtr = const_cast<Signal<T>*>(signal);
    *(signalPtr) = nullptr;
    return signalPtr->content().empty();
  }

  ///
  /// @brief Returns signal callbacks size.
  /// @return Returns any signal object callbacks count.
  ///
  template <template <typename> typename Signal, typename T>
  static std::size_t callbacks(const Signal<T>* signal) {
    return signal->size();
  }
};
}  // namespace cs

#endif  // SIGNALS_HPP
