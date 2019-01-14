#include "gtest/gtest.h"

#include "lib/system/timer.hpp"

#include <string>

TEST(Signals, BaseSignalUsingByPointer) {
  static const std::string expectedString = "Hello, world!";
  constexpr size_t expectedCalls = 2;

  class A {
  public:
    void generateSignal(const std::string& message) {
      std::cout << "Generated message: " << message << std::endl;
      emit signal(message);
    }

  public signals:
    cs::Signal<void(const std::string&)> signal;
  };

  class B {
  public:
    size_t callsCount() const {
      return callsCount_;
    }

  private:
    size_t callsCount_ = 0;

  public slots:
    void onSignalSlot(const std::string& message) {
      std::cout << "Slot message: " << message << std::endl;
      std::cout << "Slot calls count: " << ++callsCount_ << std::endl;

      ASSERT_EQ(expectedString, message);
    }
  };

  A a;
  B b;

  cs::Connector::connect(&a.signal, &b, &B::onSignalSlot);

  std::cout << cs::Connector::callbacks(&a.signal) << std::endl;
  ASSERT_EQ(cs::Connector::callbacks(&a.signal), 1);

  a.generateSignal(expectedString);
  a.generateSignal(expectedString);

  ASSERT_EQ(b.callsCount(), expectedCalls);
}

TEST(Signals, ConnectAndDisconnect) {
  struct Signaller {
  public signals:
    cs::Signal<void()> signal;
  };

  class A {
  public:
    size_t callsCount() const {
      return callsCount_;
    }

  private:
    size_t callsCount_ = 0;

  public slots:
    void onSignal() {
      ++callsCount_;
      std::cout << "OnSignal A, count " << callsCount_ << std::endl;
    }
  };

  class B {
  public:
    size_t callsCount() const {
      return callsCount_;
    }

  private:
    size_t callsCount_ = 0;

  public slots:
    void onSignal() {
      ++callsCount_;
      std::cout << "OnSignal B, count " << callsCount_ << std::endl;
    }
  };

  Signaller signaller;
  A a;
  B b;

  cs::Connector::connect(&signaller.signal, &a, &A::onSignal);
  cs::Connector::connect(&signaller.signal, &b, &B::onSignal);

  // generate signal
  emit signaller.signal();

  ASSERT_EQ(a.callsCount(), 1);
  ASSERT_EQ(b.callsCount(), 1);

  bool result = cs::Connector::disconnect(&signaller.signal, &a, &A::onSignal);
  ASSERT_EQ(result, true);

  emit signaller.signal();

  ASSERT_EQ(a.callsCount(), 1);
  ASSERT_EQ(b.callsCount(), 2);

  ASSERT_EQ(cs::Connector::callbacks(&signaller.signal), 1);

  result = cs::Connector::disconnect(&signaller.signal, &b, &B::onSignal);
  ASSERT_EQ(result, true);

  emit signaller.signal();

  ASSERT_EQ(a.callsCount(), 1);
  ASSERT_EQ(b.callsCount(), 2);

  ASSERT_EQ(cs::Connector::callbacks(&signaller.signal), 0);

  cs::Connector::connect(&signaller.signal, &a, &A::onSignal);

  emit signaller.signal();

  ASSERT_EQ(a.callsCount(), 2);
  ASSERT_EQ(b.callsCount(), 2);
}

TEST(Signals, MoveTest) {
  static std::atomic<bool> isCalled = false;
  cs::Signal<void()> signal1;

  class A {
  public slots:
    void onSignal() {
      isCalled = true;
      std::cout << "A on signal method\n";
    }
  };

  A a;
  cs::Connector::connect(&signal1, &a, &A::onSignal);

  cs::Signal<void()> signal2 = std::move(signal1);

  cs::Timer::singleShot(1000, cs::RunPolicy::ThreadPoolPolicy, [&] {
    std::cout << "Calling signal2\n";
    emit signal2();
    std::cout << "Signal2 called\n";
  });

  ASSERT_EQ(cs::Connector::callbacks(&signal2), 1);
  ASSERT_EQ(cs::Connector::callbacks(&signal1), 0);

  while(!isCalled);

  ASSERT_EQ(isCalled, true);
}
