#ifndef PROCESS_HPP
#define PROCESS_HPP

#include <thread>
#include <exception>
#include <string>
#include <vector>
#include <memory>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <boost/process.hpp>
#include <boost/process/extend.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <lib/system/signals.hpp>
#include <lib/system/processexception.hpp>

#ifdef BOOST_WINDOWS

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <boost/detail/winapi/detail/deprecated_namespace.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif

namespace cs {
using ProcessStartSignal = cs::Signal<void()>;
using ProcessFinishSignal = cs::Signal<void(int, const std::error_code&)>;
using ProcessErrorSignal = cs::Signal<void(const cs::ProcessException&)>;

///
/// @brief The Process client interface
///
class Process {
public:
    enum Options : int {
        None = 0x00,
        NewConsole = 0x01,
        OutToFile = 0x02,
        OutToStream = 0x04,
        Attach = 0x08,
        ConsoleAndToFile = NewConsole | OutToFile
    };

    explicit Process() = default;

    template <typename... Args>
    explicit Process(const std::string& program, Args&&... args);
    explicit Process(boost::process::pid_t pid);

    Process(const Process&) = delete;
    Process(Process&&) = delete;

    ~Process() noexcept;

    // program name or path to program
    void setProgram(const std::string& program);
    const std::string& program() const;

    // args to executed program
    template<typename... Args>
    void setArgs(Args&&... args);
    const std::vector<std::string>& args() const;

    // file name to redirect stdout stream
    void setFile(const std::string& file);
    const std::string& file() const;

    // returns current process running state
    bool isRunning() const;

    // returns process all out pipe data
    std::string out();
    bool isPipeValid() const;

    // waits of process end at blocking mode, better to use finished signal
    void wait();
    void terminate();

    void stop();

    bool launch(Options options = Options::None);

public signals:
    ProcessStartSignal started;
    ProcessFinishSignal finished;       // generates in io context thread
    ProcessErrorSignal errorOccured;    // may generates in another thread if thrown by io context

private:
    mutable boost::process::child process_;
    mutable std::unique_ptr<boost::process::ipstream> pipe_;

    std::string program_;
    std::vector<std::string> args_;
    boost::asio::io_context io_;
    boost::process::pid_t pid_;

    std::string file_;
};

///
/// Process internal
///

template <typename... Args>
inline cs::Process::Process(const std::string& program, Args&&... args)
: program_(program) {
    setArgs(std::forward<Args>(args)...);
}

inline cs::Process::Process(boost::process::pid_t pid)
: pid_(pid) {
}

inline Process::~Process() noexcept {
    if (isRunning()) {
        if (!process_.wait_for(std::chrono::seconds(5))) {
            terminate();
        }
        else {
            stop();
        }
    }
}

inline void Process::setProgram(const std::string& program) {
    program_ = program;
}

inline const std::string& Process::program() const {
    return program_;
}

template<typename... Args>
inline void Process::setArgs(Args&&... args) {
    (args_.push_back(args), ...);
}

inline const std::vector<std::string>& Process::args() const {
    return args_;
}

inline void Process::setFile(const std::string& file) {
    file_ = file;
}

inline const std::string& Process::file() const {
    return file_;
}

inline bool Process::isRunning() const {
    try {
        return process_.running();
    }
    catch (const std::exception& exception) {
        emit errorOccured(cs::ProcessException(exception.what()));
    }

    return false;
}

inline std::string Process::out() {
    std::string result;

    if (!pipe_ || isRunning()) {
        return result;
    }

    auto& pipe = *(pipe_.get());

    while (pipe_->good()) {
        std::string data;
        pipe >> data;

        result += data;
    }

    return result;
}

inline bool Process::isPipeValid() const {
    return static_cast<bool>(pipe_);
}

inline void Process::wait() {
    try {
        process_.wait();
        io_.stop();
    }
    catch (const std::exception& exception) {
        emit errorOccured(cs::ProcessException(exception.what()));
    }
}

inline void Process::terminate() {
    try {
        io_.stop();
        process_.terminate();
    }
    catch (const std::exception& exception) {
        emit errorOccured(cs::ProcessException(exception.what()));
    }
}

inline void Process::stop() {
    try {
        if (!io_.stopped()) {
            io_.stop();
        }
    }
    catch (const std::exception& exception) {
        emit errorOccured(cs::ProcessException(exception.what()));
    }
}

inline bool Process::launch(Process::Options options) {
    if (isRunning()) {
        return false;
    }

    auto setup = [=]([[maybe_unused]] auto& exec) {
#ifdef BOOST_WINDOWS
        if (options & Options::NewConsole) {
            exec.creation_flags |= boost::detail::winapi::CREATE_NEW_CONSOLE_;
        }
#endif
    };

    auto success = [this](auto&) {
        emit started();
    };

    auto error = [this](auto&, const std::error_code& code) {
        emit errorOccured(cs::ProcessException(code.message(), code.value()));
    };

    auto exit = [this](int code, const std::error_code& errorCode) {
        emit finished(code, errorCode);
    };

    io_.restart();
    pipe_.reset();

    try {
        if ((options & Options::OutToFile) && !file_.empty()) {
            process_ = boost::process::child(program_, args_, io_, boost::process::std_out > file_, boost::process::on_exit = exit, boost::process::extend::on_setup = setup,
                                             boost::process::extend::on_success = success, boost::process::extend::on_error = error);
        }
        else if (options & Options::OutToStream) {
            pipe_ = std::make_unique<boost::process::ipstream>();
            process_ = boost::process::child(program_, args_, io_, boost::process::std_out > (*pipe_.get()), boost::process::on_exit = exit, boost::process::extend::on_setup = setup,
                                             boost::process::extend::on_success = success, boost::process::extend::on_error = error);
        }
        else if (options & Options::Attach) {
            process_ = boost::process::child(pid_);
        }
        else {
            process_ = boost::process::child(program_, args_, io_, boost::process::on_exit = exit, boost::process::extend::on_setup = setup,
                                             boost::process::extend::on_success = success, boost::process::extend::on_error = error);
        }
    }
    catch (const std::exception& exception) {
        pipe_.reset();
        emit errorOccured(cs::ProcessException(exception.what()));

        return false;
    }

    if (options & Options::Attach) {
        return true;
    }

    try {
        std::thread thread([this] {
            try {
                io_.run();
            }
            catch (const std::exception& exception) {
                terminate();
                emit errorOccured(cs::ProcessException(exception.what()));
            }
        });

        thread.detach();
    }
    catch (const std::exception& exception) {
        terminate();
        emit errorOccured(cs::ProcessException(exception.what()));

        return false;
    }

    return true;
}
}

#endif // PROCESS_HPP
