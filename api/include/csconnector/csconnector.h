#pragma once
#include <APIHandler.h>
#include <DebugLog.h>

#include <thread>
#include <memory>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/server/TThreadedServer.h>

#include <csdb/storage.h>
#include <Solver/ISolver.hpp>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using namespace api;

namespace csconnector {

    struct Config {
        int port = 9090;
    };

    class csconnector {
    public:

        csconnector(BlockChain &m_blockchain, Credits::ISolver* solver, const Config &config = Config{});
        ~csconnector();


        csconnector(const csconnector &) = delete;
        csconnector &operator=(const csconnector &)= delete;
    private:
        TThreadedServer server;
        std::thread thread;
    };
}
