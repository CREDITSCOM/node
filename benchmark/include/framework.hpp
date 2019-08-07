#ifndef FRAMEWORK_HPP
#define FRAMEWORK_HPP

#include <type_traits>
#include <benchmark.hpp>

namespace cs {
class Framework {
public:
    template <typename Func, typename Result = std::invoke_result_t<Func>>
    static Result execute(Func func, std::chrono::seconds max = std::chrono::seconds(30), [[maybe_unused]] const std::string& failedMessage = std::string{}) {
        try {
            if constexpr (std::is_same_v<Result, bool>) {
                auto result = cs::Benchmark<bool>::run(func, max);

                if (!result) {
                    cs::Console::writeLine("\n", failedMessage);
                    cs::Console::writeLine("Failed running other tests, fix previous test to correct execution");

                    std::exit(0);
                }

                return result;
            }
            else if constexpr (std::is_same_v<Result, void>) {
                cs::Benchmark<void>::run(func, max);
            }
            else {
                return cs::Benchmark<Result>::run(func, max);
            }
        }
        catch (const std::exception& e) {
            cs::Console::writeLine("Exception catched, message: ", e.what());
            cs::Console::writeLine("Benchmark would be terminated");

            std::terminate();
        }
    }
};
}  // namespace cs

#endif // FRAMEWORK_HPP

