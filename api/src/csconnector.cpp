#include "stdafx.h"

#include <csdb/currency.hpp>
#if defined(_MSC_VER)
#pragma warning(push)
// 4245: 'return': conversion from 'int' to 'SOCKET', signed/unsigned mismatch
#pragma warning(disable: 4245)
#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/transport/THttpServer.h>
#pragma warning(pop)
#endif // _MSC_VER
#include "csconnector/csconnector.hpp"

namespace csconnector {

using ::apache::thrift::TProcessorFactory;

using namespace ::apache::thrift::stdcxx;
using namespace ::apache::thrift::server;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::protocol;

connector::connector(BlockChain& m_blockchain, cs::SolverCore* solver, const Config& config)
: api_handler(make_shared<api::APIHandler>(m_blockchain, *solver))
, api_processor(api_handler)
, p_api_processor_factory(new api::SequentialProcessorFactory(api_processor))
#ifdef BINARY_TCP_API
, server(p_api_processor_factory, make_shared<TServerSocket>(config.port), make_shared<TBufferedTransportFactory>(),
         make_shared<TBinaryProtocolFactory>())
#endif
#ifdef AJAX_IFACE
, ajax_server(p_api_processor_factory, make_shared<TServerSocket>(config.ajax_port),
              make_shared<THttpServerTransportFactory>(), make_shared<TJSONProtocolFactory>())
#endif
{
#ifdef BINARY_TCP_API
  thread = std::thread([this]() {
    try {
      server.run();
    }
    catch (...) {
      std::cerr << "Oh no! I'm dead :'-(" << std::endl;
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
      std::cerr << "Oh no! I'm dead in AJAX :'-(" << std::endl;
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

#ifdef AJAX_IFACE
  ajax_server.stop();
  if (ajax_thread.joinable()) {
    ajax_thread.join();
  }
#endif
}
}  // namespace csconnector
