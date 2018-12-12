#include "gtest/gtest.h"
#include "lib/system/signals.hpp"
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

  std::cout << cs::Connector::callbacks(a.signal) << std::endl;
  ASSERT_EQ(cs::Connector::callbacks(a.signal), 1);

  a.generateSignal(expectedString);
  a.generateSignal(expectedString);

  ASSERT_EQ(b.callsCount(), expectedCalls);
}
