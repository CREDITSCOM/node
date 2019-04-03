#include "stdafx.h"

#include <csdb/currency.hpp>
#if defined(_MSC_VER)
#pragma warning(push)
// 4245: 'return': conversion from 'int' to 'SOCKET', signed/unsigned mismatch
#pragma warning(disable: 4245)
#endif
#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/transport/THttpServer.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif  // _MSC_VER

#include "csconnector/csconnector.hpp"

namespace csconnector {

using ::apache::thrift::TProcessorFactory;

using namespace ::apache::thrift::stdcxx;
using namespace ::apache::thrift::server;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::protocol;

connector::connector(BlockChain& m_blockchain, cs::SolverCore* solver, const Config& config)
: executor_(executor::Executor::getInstance(&m_blockchain, solver, config.executor_port))
, api_handler(make_shared<api::APIHandler>(m_blockchain, *solver, executor_, config))
, apiexec_handler(make_shared<apiexec::APIEXECHandler>(m_blockchain, *solver, executor_, config))
, p_api_processor(make_shared<api::APIProcessor>(api_handler))
, p_apiexec_processor(make_shared<apiexec::APIEXECProcessor>(apiexec_handler))
#ifdef BINARY_TCP_API
, server(p_api_processor, make_shared<TServerSocket>(config.port), make_shared<TBufferedTransportFactory>(),
         make_shared<TBinaryProtocolFactory>())
#endif
#ifdef AJAX_IFACE
, ajax_server(p_api_processor, make_shared<TServerSocket>(config.ajax_port),
              make_shared<THttpServerTransportFactory>(), make_shared<TJSONProtocolFactory>())
#endif
#ifdef BINARY_TCP_EXECAPI
, exec_server(p_apiexec_processor, make_shared<TServerSocket>(config.apiexec_port), make_shared<TBufferedTransportFactory>(),
  make_shared<TBinaryProtocolFactory>())
#endif
{
  cslog() << "Api port " << config.port << ", ajax port " << config.ajax_port;

#ifdef BINARY_TCP_EXECAPI
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
