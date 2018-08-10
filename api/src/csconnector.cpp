#include "csconnector/csconnector.h"
#include <csdb/currency.h>
#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/transport/THttpServer.h>

#define TRACE()

namespace csconnector {

using namespace stdcxx;

csconnector::csconnector(BlockChain& m_blockchain,
                         Credits::ISolver* solver,
                         const Config& config)
  : api_handler(make_shared<APIHandler>(m_blockchain, *solver))
  , api_processor(make_shared<APIProcessor>(api_handler))
  , server(api_processor,
           make_shared<TServerSocket>(config.port),
           make_shared<TBufferedTransportFactory>(),
           make_shared<TBinaryProtocolFactory>())
#ifdef AJAX_IFACE
  , ajax_server(api_processor,
                make_shared<TServerSocket>(config.ajax_port),
                make_shared<THttpServerTransportFactory>(),
                make_shared<TJSONProtocolFactory>())
#endif
{
    thread = std::thread([this, config]() {
        try {
            Log("csconnector started on port ", config.port);
            server.run();
        } catch (...) {
            std::cerr << "Oh no! I'm dead :'-(" << std::endl;
        }
    });
#ifdef AJAX_IFACE
    ajax_thread = std::thread([this, config]() {
        try {
            Log("csconnector for AJAX started on port ", config.ajax_port);
            ajax_server.run();
        } catch (...) {
            std::cerr << "Oh no! I'm dead in AJAX :'-(" << std::endl;
        }
    });
#endif
}

csconnector::~csconnector()
{
    server.stop();
    if (thread.joinable()) {
        thread.join();
    }

#ifdef AJAX_IFACE
    ajax_server.stop();
    if (ajax_thread.joinable()) {
        ajax_thread.join();
    }
#endif

    Log("csconnector stopped");
}
}
