#ifndef PROCESSEXCEPTION_HPP
#define PROCESSEXCEPTION_HPP

#include <exception>
#include <string>

namespace cs {
class ProcessException : public std::exception {
public:
    explicit ProcessException(const std::string& message, int code = 0)
    : message_(message)
    , code_(code) {
    }

    ProcessException(const ProcessException&) = default;
    ProcessException(ProcessException&&) = default;

    virtual const char* what() const noexcept override {
        return message_.c_str();
    }

    int code() const {
        return code_;
    }

private:
    std::string message_;
    int code_ = 0;
};
}

#endif // PROCESSEXCEPTION_HPP
