#ifndef CSCONNCETOR_HPP
#define CSCONNCETOR_HPP

#include <apihandler.hpp>

#include <memory>
#include <thread>

#if defined(_MSC_VER)
#pragma warning(push)
// 4245: 'return': conversion from 'int' to 'SOCKET', signed/unsigned mismatch
#pragma warning(disable: 4245)
#endif
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/server/TThreadedServer.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <solvercore.hpp>

#include <client/params.hpp>

namespace csconnector {

struct Config {
  int port = 9090;
#ifdef AJAX_IFACE
  int ajax_port = 8081;
#endif
  int executor_port = 9080;
};

class connector {
public:
  using ApiHandlerPtr = ::apache::thrift::stdcxx::shared_ptr<api::APIHandler>;

  explicit connector(BlockChain& m_blockchain, cs::SolverCore* solver, const Config& config = Config{});
  ~connector();

  connector(const connector&) = delete;
  connector& operator=(const connector&) = delete;

  // interface
  ApiHandlerPtr apiHandler() const;

private:
  ApiHandlerPtr api_handler;
  ::api::custom::APIProcessor api_processor;
  ::apache::thrift::stdcxx::shared_ptr<api::SequentialProcessorFactory> p_api_processor_factory;
#ifdef BINARY_TCP_API
  ::apache::thrift::server::TThreadedServer server;
  std::thread thread;
#endif
#ifdef AJAX_IFACE
  ::apache::thrift::server::TThreadedServer ajax_server;
  std::thread ajax_thread;
#endif
  friend class ::api::custom::APIProcessor;
};
}  // namespace csconnector

#endif  // CSCONNCETOR_HPP
