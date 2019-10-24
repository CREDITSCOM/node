#include <framework.hpp>

#include <lib/system/signals.hpp>
#include <lib/system/random.hpp>
#include <lib/system/console.hpp>

static volatile size_t result = 0;

static inline void common() {
    result = cs::Random::generateValue<size_t>(0, 100);
}

class A {
public signals:
    cs::Signal<void()> called;
};

class B {
public slots:
    void onCalled() {
        common();
    }
};

class C {
public:
    virtual ~C() = default;

    virtual void call() {
        common();
    }
};

class D : public C {
    virtual void call() override {
        common();
    }
};

static A a;
static B b;
static C* c = new C();
static C* d = new D();

static const size_t callsCount = 100'000'000;
static const std::function<void()> bindedFunction = std::bind(&B::onCalled, &b);

static void runDirectCall() {
    for (size_t i = 0; i < callsCount; ++i) {
        b.onCalled();
    }
}

static void runSignalsCall() {
    for (size_t i = 0; i < callsCount; ++i) {
        a.called();
    }
}

static void runBindedFunctionCall() {
    for (size_t i = 0; i < callsCount; ++i) {
        bindedFunction();
    }
}

static void runVirtualDirectCall() {
    for (size_t i = 0; i < callsCount; ++i) {
        c->call();
    }
}

static void runVirtualDerivedCall() {
    for (size_t i = 0; i < callsCount; ++i) {
        d->call();
    }
}

static void testDirectCall() {
    cs::Console::writeLine("Test direct method call");
    cs::Framework::execute(&runDirectCall);
    cs::Console::writeLine("");
}

static void testSignals() {
    cs::Console::writeLine("Test signal call");
    cs::Framework::execute(&runSignalsCall);
    cs::Console::writeLine("");
}

static void testBindedFunction() {
    cs::Console::writeLine("Test std::function call");
    cs::Framework::execute(&runBindedFunctionCall);
    cs::Console::writeLine("");
}

static void testVirtualDirectCall() {
    cs::Console::writeLine("Test virtual direct call");
    cs::Framework::execute(&runVirtualDirectCall);
    cs::Console::writeLine("");
}

static void testVirtualDerivedCall() {
    cs::Console::writeLine("Test virtual derived call");
    cs::Framework::execute(&runVirtualDerivedCall);
    cs::Console::writeLine("");
}

int main() {
    cs::Connector::connect(&a.called, &b, &B::onCalled);

    testDirectCall();
    testSignals();
    testBindedFunction();
    testVirtualDirectCall();
    testVirtualDerivedCall();

    delete c;
    delete d;

    return 0;
}
