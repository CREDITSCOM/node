#pragma once
#include <APIHandler.h>
#include <DebugLog.h>

#include <memory>
#include <thread>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include <Solver/ISolver.hpp>
#include <csdb/storage.h>

#include <client/params.hpp>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using namespace api;

namespace csconnector {

struct Config
{
    int port = 9090;
#ifdef AJAX_IFACE
    int ajax_port = 80;
#endif
};

class csconnector
{
  public:
    csconnector(BlockChain& m_blockchain,
                Credits::ISolver* solver,
                const Config& config = Config{});
    ~csconnector();

    csconnector(const csconnector&) = delete;
    csconnector& operator=(const csconnector&) = delete;

  private:
    stdcxx::shared_ptr<APIIf> api_handler;
    stdcxx::shared_ptr<APIProcessor> api_processor;
    TThreadedServer server;
    std::thread thread;
#ifdef AJAX_IFACE
    TThreadedServer ajax_server;
    std::thread ajax_thread;
#endif
};
}
