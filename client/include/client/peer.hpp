#pragma once

#include <memory>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <params.hpp>
#include <lib/system/service/service.hpp>
#include <csnode/node.hpp>
#include <observer.hpp>

#include <boost/program_options.hpp>

namespace cs {

class Peer : public ServiceOwner {
public:
    Peer(
        const char* serviceName,
        Config&,
        boost::program_options::variables_map&
    );
    int executeProtocol();

protected:
    // ServiceOwner interface
    bool onInit(const char* serviceName) override;
    bool onRun(const char* serviceName) override;
    bool onStop() override;
    bool onPause() override;
    bool onContinue() override;

private:
    Service                                service_;
    std::unique_ptr<Node>                  node_;
    std::unique_ptr<config::Observer>      observer_;
    Config&                                config_;
    boost::program_options::variables_map& vm_;
};

} // namespace cs
