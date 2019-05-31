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

    cs::Timer::singleShot(1000, cs::RunPolicy::ThreadPolicy, [&] {
        std::cout << "Calling signal2\n";
        emit signal2();
        std::cout << "Signal2 called\n";
    });

    ASSERT_EQ(cs::Connector::callbacks(&signal2), 1);
    ASSERT_EQ(cs::Connector::callbacks(&signal1), 0);

    while (!isCalled);

    ASSERT_EQ(isCalled, true);
}

void func(const std::string& message, std::reference_wrapper<bool> wrapper) {
    wrapper.get() = true;
    std::cout << message << std::endl;
}

TEST(Signals, LambdaAndFuncConnections) {
    static bool isLambdaCalled = false;
    static bool isFunctionCalled = false;

    cs::Signal<void(const std::string&, std::reference_wrapper<bool>)> signal1;
    cs::Connector::connect(&signal1, &func);

    auto lambda = [&](const std::string& message, std::reference_wrapper<bool> wrapper) {
        wrapper.get() = true;
        std::cout << "Lambda message - " << message << std::endl;
    };

    cs::Signal<void(const std::string&, std::reference_wrapper<bool>)> signal2;
    cs::Connector::connect(&signal2, lambda);

    emit signal1("Hello, world!", std::ref(isFunctionCalled));
    emit signal2("Credits tests", std::ref(isLambdaCalled));

    ASSERT_EQ(isFunctionCalled, true);
    ASSERT_EQ(isLambdaCalled, true);
}

TEST(Signals, SignalToSignalConnection) {
    static bool isCalled = false;
    cs::Signal<void()> signal1;
    cs::Signal<void()> signal2;

    cs::Connector::connect(&signal2, [&] {
        isCalled = true;
        std::cout << "Lambda called\n";
    });

    cs::Connector::connect(&signal1, &signal2);

    emit signal1();

    ASSERT_EQ(isCalled, true);
}

size_t foo() {
    static size_t callsCount = 0;
    std::cout << "Foo calls count " << ++callsCount << std::endl;

    return callsCount;
}

TEST(Signals, LambdaAndFunctionConnectionDisconnection) {
    class A {
    public signals:
        cs::Signal<void()> signal;
    };

    auto a = std::make_shared<A>();
    cs::Connector::connect(&(a->signal), &foo);

    auto lambda = []() {
        static size_t callsCount = 0;
        std::cout << "Lambda calls count " << ++callsCount << std::endl;

        return callsCount;
    };

    cs::Connector::connect(&(a->signal), lambda);

    emit a->signal();

    ASSERT_EQ(cs::Connector::callbacks(&(a->signal)), 2);
    ASSERT_EQ(foo(), 2);
    ASSERT_EQ(lambda(), 2);

    bool result = cs::Connector::disconnect(&(a->signal), &foo);
    ASSERT_EQ(result, true);

    emit a->signal();

    ASSERT_EQ(cs::Connector::callbacks(&(a->signal)), 1);
    ASSERT_EQ(foo(), 3);
    ASSERT_EQ(lambda(), 4);

    cs::Connector::connect(&(a->signal), &foo);

    emit a->signal();

    ASSERT_EQ(cs::Connector::callbacks(&(a->signal)), 2);
    ASSERT_EQ(foo(), 5);
    ASSERT_EQ(lambda(), 6);

    result = cs::Connector::disconnect(&(a->signal), lambda);
    ASSERT_EQ(result, true);

    emit a->signal();

    ASSERT_EQ(cs::Connector::callbacks(&(a->signal)), 1);
    ASSERT_EQ(foo(), 7);
    ASSERT_EQ(lambda(), 7);

    cs::Connector::disconnect(&(a->signal));

    emit a->signal();

    ASSERT_EQ(cs::Connector::callbacks(&(a->signal)), 0);
    ASSERT_EQ(foo(), 8);
    ASSERT_EQ(lambda(), 8);
}

TEST(Signals, ConstObjectConnection) {
    static bool isCalled = false;

    class A {
    public signals:
        cs::Signal<void()> signal;
    };

    class B {
    public slots:
        void onSignal() const {
            std::cout << "B on signal called" << std::endl;
            isCalled = true;
        }
    };

    const A a;
    const B b;

    cs::Connector::connect(&a.signal, &b, &B::onSignal);

    emit a.signal();
    ASSERT_TRUE(isCalled);
}

TEST(Signals, UnexpectedDisconnect) {
    static bool isCalled = false;

    class A {
    public signals:
        cs::Signal<void(const std::string&)> signal;
    };

    class B : public cs::IConnectable {
    public slots:
        void onSignal(const std::string& msg) {
            isCalled = true;
            cs::Console::writeLine(msg);
        }
    };

    A a;

    {
        B b;
        cs::Connector::connect(&a.signal, &b, &B::onSignal);
    }

    emit a.signal("cs");

    ASSERT_EQ(isCalled, false);
    ASSERT_EQ(cs::Connector::callbacks(&a.signal), 0);
}

class Checker {
public:
    Checker(bool& f)
    : flag(f) {
    }

    Checker(const Checker& checker)
    : flag(checker.flag) {
        flag = true;
    }

    Checker& operator=(const Checker&) {
        flag = true;
        return *this;
    }

    size_t value = 0;
    bool& flag;
};

TEST(Signals, NonArgumentsCopy) {
    static bool isCopied = false;
    constexpr size_t expectedValue = 10;

    class A {
    public signals:
        cs::Signal<void(Checker&)> signal;
    };

    class B {
    public slots:
        void onSignal(Checker& checker) {
            cs::Console::writeLine("B slot called");
            checker.value = expectedValue;
        }
    };

    A a;
    B b;
    Checker checker(isCopied);

    cs::Connector::connect(&a.signal, &b, &B::onSignal);

    emit a.signal(checker);

    ASSERT_EQ(isCopied, false);
    ASSERT_EQ(checker.value, expectedValue);
}

TEST(Signals, ArgumentsCopy) {
    static bool isCopied = false;

    class A {
    public signals:
        cs::Signal<void(Checker)> signal;
    };

    class B {
    public slots:
        void onSignal([[maybe_unused]] Checker checker) {
            cs::Console::writeLine("B slot called");
            checker.value = 10;
        }
    };

    A a;
    B b;
    Checker checker(isCopied);

    cs::Connector::connect(&a.signal, &b, &B::onSignal);

    emit a.signal(checker);

    ASSERT_EQ(isCopied, true);
    ASSERT_EQ(checker.value, 0);
}
