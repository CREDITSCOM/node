#ifndef PROFILEREVENTHANDLER_HPP
#define PROFILEREVENTHANDLER_HPP

#include <thrift/server/TServer.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransport.h>

#include <profiler/profiler.hpp>

namespace cs {
class ProfilerEventHandler : public apache::thrift::server::TServerEventHandler {
public:
    virtual void processContext(void* serverContext,
                                apache::thrift::stdcxx::shared_ptr<apache::thrift::protocol::TTransport> transport) override {
        auto socket = static_cast<::apache::thrift::transport::TSocket *>(transport.get());

        cs::ProfilerFileLogger::instance().add(socket->getSocketInfo());
        apache::thrift::server::TServerEventHandler::processContext(serverContext, transport);
    }
};
}

#endif // PROFILEREVENTHANDLER_HPP

