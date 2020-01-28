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
#include <csnode/transactionspacket.hpp>

namespace csconnector {

using ::apache::thrift::TProcessorFactory;

using namespace ::apache::thrift::stdcxx;
using namespace ::apache::thrift::server;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::protocol;

constexpr const int32_t kRestartThriftPause_ms = 200; // milliseconds
const int32_t kStringLimit = static_cast<int32_t>(Consensus::MaxTransactionSize);
constexpr const int32_t kContainerLimit = 16 * 1024; // max allowed items in any container (map, list, set)
constexpr const bool kStrictRead = false; // use default Thrift value
constexpr const bool kStrictWrite = true; // use default Thrift value

connector::connector(BlockChain& m_blockchain, cs::SolverCore* solver)
: executor_(cs::Executor::instance())
, api_handler(make_shared<api::APIHandler>(m_blockchain, *solver, executor_))
, apiexec_handler(make_shared<apiexec::APIEXECHandler>(m_blockchain, *solver, executor_))
, p_api_processor(make_shared<connector::ApiProcessor>(api_handler))
, p_apiexec_processor(make_shared<apiexec::APIEXECProcessor>(apiexec_handler))
{

#ifdef BINARY_TCP_EXECAPI

    execapi_thread = std::thread([this]() {
    
        while (true) {
            const uint16_t exec_server_port = uint16_t(cs::ConfigHolder::instance().config()->getApiSettings().apiexecPort);
            cslog() << "Starting executor API on port " << exec_server_port;
            execapi_server = std::make_shared<TThreadedServer>(
                p_apiexec_processor,
                make_shared<TServerSocket>(exec_server_port),
                make_shared<TBufferedTransportFactory>(),
                make_shared<TBinaryProtocolFactory>(kStringLimit, kContainerLimit, kStrictRead, kStrictWrite)
            );
            try {
                std::shared_ptr<TThreadedServer> srv = execapi_server;
                srv->run();
                cslog() << "Stop executor API on port " << exec_server_port;
                break;
            }
            catch (...) {
                cserror() << "Executor API stopped unexpectedly";
            }
            // wait before restarting server
            std::this_thread::sleep_for(std::chrono::milliseconds(kRestartThriftPause_ms));
        }

    });

#endif
}

void connector::run() {

    using ::apache::thrift::server::TThreadedServer;

#ifdef BINARY_TCP_API
   
    api_thread = std::thread([this]() {

        while (true) {
            const auto& config = cs::ConfigHolder::instance().config()->getApiSettings();
            const uint16_t api_port = uint16_t(config.port);
            cslog() << "Starting public API on port " << api_port;
            api_server = std::make_unique<TThreadedServer>(
                p_api_processor,
                make_shared<TServerSocket>(api_port, config.serverSendTimeout, config.serverReceiveTimeout),
                make_shared<TBufferedTransportFactory>(),
                make_shared<TBinaryProtocolFactory>(kStringLimit, kContainerLimit, kStrictRead, kStrictWrite)
            );

#ifdef PROFILE_API
            cs::ProfilerFileLogger::bufferSize = 1000;
            api_server->setServerEventHandler(make_shared<cs::ProfilerEventHandler>());
#endif

            try {
                std::shared_ptr<TThreadedServer> srv = api_server;
                srv->run();
                cslog() << "Stop public API on port " << api_port;
                break;
            }
            catch (...) {
                cserror() << "Public API stopped unexpectedly";
            }
            // wait before restarting server
            std::this_thread::sleep_for(std::chrono::milliseconds(kRestartThriftPause_ms));
        }

    });

#endif

#ifdef AJAX_IFACE

    ajax_thread = std::thread([this]() {

        while (true) {
            const auto& config = cs::ConfigHolder::instance().config()->getApiSettings();
            const uint16_t ajax_port = uint16_t(config.ajaxPort);
            cslog() << "Starting AJAX server on port " << ajax_port;
            ajax_server = std::make_unique<TThreadedServer>(
                p_api_processor,
                make_shared<TServerSocket>(ajax_port, config.ajaxServerSendTimeout, config.ajaxServerReceiveTimeout),
                make_shared<THttpServerTransportFactory>(),
                make_shared<TJSONProtocolFactory>()
            );

            try {
                std::shared_ptr<TThreadedServer> srv = ajax_server;
                srv->setConcurrentClientLimit(AJAX_CONCURRENT_API_CLIENTS);
                srv->run();
                cslog() << "Stop public AJAX server on port " << ajax_port;
                break;
            }
            catch (...) {
                cserror() << "AJAX server stopped unexpectedly";
            }
            // wait before restarting server
            std::this_thread::sleep_for(std::chrono::milliseconds(kRestartThriftPause_ms));
        }

    });

#endif

    api_handler->run();
}

connector::~connector() {
    stop();
}

void connector::stop() {

#ifdef BINARY_TCP_API
    if (api_server) {
        api_server->stop();
        api_server.reset();
        if (api_thread.joinable()) {
            api_thread.join();
        }
    }
#endif

#ifdef BINARY_TCP_EXECAPI
    if (execapi_server) {
        execapi_server->stop();
        execapi_server.reset();
        if (execapi_thread.joinable()) {
            execapi_thread.join();
        }
    }
#endif

#ifdef AJAX_IFACE
    if (ajax_server) {
        ajax_server->stop();
        ajax_server.reset();
        if (ajax_thread.joinable()) {
            ajax_thread.join();
        }
    }
#endif
}

void connector::onPacketExpired(const cs::TransactionsPacket& packet) {
    api_handler->onPacketExpired(packet);
}

void connector::onTransactionsRejected(const cs::TransactionsPacket& packet) {
    api_handler->onTransactionsRejected(packet);
}

connector::ApiHandlerPtr connector::apiHandler() const {
    return api_handler;
}

connector::ApiExecHandlerPtr connector::apiExecHandler() const {
    return apiexec_handler;
}

}  // namespace csconnector
