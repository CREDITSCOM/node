#ifndef CSCONNCETOR_HPP
#define CSCONNCETOR_HPP

#if defined(_MSC_VER)
#pragma warning(push, 0) // 4245: 'return': conversion from 'int' to 'SOCKET', signed/unsigned mismatch
#endif

#include <apihandler.hpp>

#include <thrift/server/TThreadPoolServer.h>
#include <thrift/server/TThreadedServer.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <client/params.hpp>

#include <csdb/pool.hpp>

#include <solvercore.hpp>

#include <memory>
#include <thread>

#include <apidiaghandler.hpp>

#ifdef PROFILE_API
#include <profiler/profilerprocessor.hpp>
#include <profiler/profilereventhandler.hpp>
#endif

namespace cs {
class TransactionsPacket;
}
class Node;

namespace csconnector {
class connector {
public:
    using ApiHandlerPtr = ::apache::thrift::stdcxx::shared_ptr<api::APIHandler>;
    using ApiExecHandlerPtr = ::apache::thrift::stdcxx::shared_ptr<apiexec::APIEXECHandler>;
    using DiagHandlerPtr = ::apache::thrift::stdcxx::shared_ptr<api_diag::APIDiagHandler>;

#ifdef PROFILE_API
    using ApiProcessor = cs::ProfilerProcessor;
#else
    using ApiProcessor = ::api::APIProcessor;
#endif

    explicit connector(Node& node);
    ~connector();

    connector(const connector&) = delete;
    connector& operator=(const connector&) = delete;

    void onReadFromDB(csdb::Pool pool, bool* should_stop) {
        if (!*should_stop) {
            api_handler->updateSmartCachesPool(pool);
#ifdef MONITOR_NODE
            api_handler->collect_all_stats_slot(pool);
#endif
            api_handler->baseLoaded(pool);
        }
    }

    void onStoreBlock(const csdb::Pool& pool) {
        api_handler->store_block_slot(pool);
    }

    void onMaxBlocksCount(cs::Sequence lastBlockNum) {
        api_handler->maxBlocksCount(lastBlockNum);
    }

public slots:
    void onPacketExpired(const cs::TransactionsPacket& packet);
    void onTransactionsRejected(const cs::TransactionsPacket& packet);

public:
    void run();
    void stop();

    // interface
    ApiHandlerPtr apiHandler() const;
    ApiExecHandlerPtr apiExecHandler() const;

private:
    cs::Executor& executor_;
    ApiHandlerPtr api_handler;
    ApiExecHandlerPtr apiexec_handler;
    DiagHandlerPtr diag_handler;

    ::apache::thrift::stdcxx::shared_ptr<ApiProcessor> api_processor;
    ::apache::thrift::stdcxx::shared_ptr<::apiexec::APIEXECProcessor> apiexec_processor;
    ::apache::thrift::stdcxx::shared_ptr<::api_diag::API_DIAGProcessor> diag_processor;

#ifdef BINARY_TCP_API
    std::thread api_thread;
    std::shared_ptr<::apache::thrift::server::TThreadedServer> api_server;
#endif

#ifdef AJAX_IFACE
    std::thread ajax_thread;
    std::shared_ptr<::apache::thrift::server::TThreadedServer> ajax_server;
#endif

#ifdef BINARY_TCP_EXECAPI
    std::thread execapi_thread;
    std::shared_ptr<::apache::thrift::server::TThreadedServer> execapi_server;
#endif

#if defined(DIAG_API)
    std::thread diag_thread;
    std::shared_ptr<::apache::thrift::server::TThreadedServer> diag_server;
#endif // DIAG_API

    std::atomic_bool stop_flag;
};
}  // namespace csconnector

#endif  // CSCONNCETOR_HPP
