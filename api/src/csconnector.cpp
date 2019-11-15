#include "stdafx.h"

#include <csdb/currency.hpp>

#if defined(_MSC_VER)
#pragma warning(push, 0) // 4245: 'return': conversion from 'int' to 'SOCKET', signed/unsigned mismatch
#endif

#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/transport/THttpServer.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif  // _MSC_VER

#include "csconnector/csconnector.hpp"

#include <csnode/configholder.hpp>

namespace csconnector {

using ::apache::thrift::TProcessorFactory;

using namespace ::apache::thrift::stdcxx;
using namespace ::apache::thrift::server;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::protocol;

connector::connector(BlockChain& m_blockchain, cs::SolverCore* solver)
: executor_(executor::Executor::getInstance())
, api_handler(make_shared<api::APIHandler>(m_blockchain, *solver, executor_))
, apiexec_handler(make_shared<apiexec::APIEXECHandler>(m_blockchain, *solver, executor_))
, p_api_processor(make_shared<connector::ApiProcessor>(api_handler))
, p_apiexec_processor(make_shared<apiexec::APIEXECProcessor>(apiexec_handler))
#ifdef BINARY_TCP_API
, server(p_api_processor, make_shared<TServerSocket>(cs::ConfigHolder::instance().config()->getApiSettings().port,
                                                     cs::ConfigHolder::instance().config()->getApiSettings().serverSendTimeout,
                                                     cs::ConfigHolder::instance().config()->getApiSettings().serverReceiveTimeout),
    make_shared<TBufferedTransportFactory>(), make_shared<TBinaryProtocolFactory>())
#endif
#ifdef AJAX_IFACE
, ajax_server(p_api_processor, make_shared<TServerSocket>(cs::ConfigHolder::instance().config()->getApiSettings().ajaxPort,
                                                          cs::ConfigHolder::instance().config()->getApiSettings().ajaxServerSendTimeout,
                                                          cs::ConfigHolder::instance().config()->getApiSettings().ajaxServerReceiveTimeout),
    make_shared<THttpServerTransportFactory>(), make_shared<TJSONProtocolFactory>())
#endif
#ifdef BINARY_TCP_EXECAPI
, exec_server(p_apiexec_processor, make_shared<TServerSocket>(cs::ConfigHolder::instance().config()->getApiSettings().apiexecPort),
    make_shared<TBufferedTransportFactory>(), make_shared<TBinaryProtocolFactory>())
#endif
{
#ifdef PROFILE_API
    cs::ProfilerFileLogger::bufferSize = 1000;
    server.setServerEventHandler(make_shared<cs::ProfilerEventHandler>());
#endif

#ifdef BINARY_TCP_EXECAPI
    exec_server_port = uint16_t(cs::ConfigHolder::instance().config()->getApiSettings().apiexecPort);
    cslog() << "Starting executor API on port " << cs::ConfigHolder::instance().config()->getApiSettings().apiexecPort;
    exec_thread = std::thread([this]() {
        try {
            exec_server.run();
        }
        catch (...) {
            cserror() << "Oh no! I'm dead :'-(";
        }
    });
#endif

#ifdef BINARY_TCP_API
    server_port = uint16_t(cs::ConfigHolder::instance().config()->getApiSettings().port);
#endif

#ifdef AJAX_IFACE
    ajax_server_port = uint16_t(cs::ConfigHolder::instance().config()->getApiSettings().ajaxPort);
#endif
}

void connector::run() {

#ifdef BINARY_TCP_API
    cslog() << "Starting public API on port " << server_port;
    thread = std::thread([this]() {
        try {
            server.run();
        }
        catch (...) {
            cserror() << "Oh no! I'm dead :'-(";
        }
        });
#endif

#ifdef AJAX_IFACE
    cslog() << "Starting AJAX server on port " << ajax_server_port;
    ajax_server.setConcurrentClientLimit(AJAX_CONCURRENT_API_CLIENTS);
    ajax_thread = std::thread([this]() {
        try {
            ajax_server.run();
        }
        catch (...) {
            cserror() << "Oh no! I'm dead in AJAX :'-(";
        }
        });
#endif

    api_handler->run();
}

connector::~connector() {
#ifdef BINARY_TCP_API
    server.stop();
    if (thread.joinable()) {
        thread.join();
    }
#endif

#ifdef BINARY_TCP_EXECAPI
    exec_server.stop();
    if (exec_thread.joinable()) {
        exec_thread.join();
    }
#endif

#ifdef AJAX_IFACE
    ajax_server.stop();
    if (ajax_thread.joinable()) {
        ajax_thread.join();
    }
#endif
}

connector::ApiHandlerPtr connector::apiHandler() const {
    return api_handler;
}

connector::ApiExecHandlerPtr connector::apiExecHandler() const {
    return apiexec_handler;
}

}  // namespace csconnector
