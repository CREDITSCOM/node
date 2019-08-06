#ifndef FRAMEWORK_HPP
#define FRAMEWORK_HPP

#include <benchmark.hpp>

namespace cs {
class Framework {
public:
    template <typename Func>
    static void execute(Func func, const std::string& failedMessage = std::string{}, size_t count = 1) {
        for (size_t i = 0; i < count; ++i) {
            try {
                if (!cs::Benchmark::run(func)) {
                    cs::Console::writeLine("\n", failedMessage, " on ", i + 1, " iteration");
                    cs::Console::writeLine("Failed running other tests, fix previous test to correct execution");

                    std::exit(0);
                }
            }
            catch (const std::exception& e) {
                cs::Console::writeLine("Exception catched, message: ", e.what());
                cs::Console::writeLine("Benchmark would be terminated");

                std::terminate();
            }
        }
    }
};
}  // namespace cs

#endif // FRAMEWORK_HPP

