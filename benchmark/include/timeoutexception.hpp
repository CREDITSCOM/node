#ifndef TIMEOUTEXCEPTION_HPP
#define TIMEOUTEXCEPTION_HPP

#include <exception>

namespace cs {
class TimeOutException : public std::exception {
public:
    virtual const char* what() const noexcept override {
        return "Benchmark Time out";
    }
};
}

#endif // TIMEOUTEXCEPTION_HPP
