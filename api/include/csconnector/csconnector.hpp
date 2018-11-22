#ifndef CSCONNCETOR_HPP
#define CSCONNCETOR_HPP

#include <apihandler.hpp>

#include <memory>
#include <thread>

#include <thrift/server/TThreadPoolServer.h>
#include <thrift/server/TThreadedServer.h>

#include <solver2/solvercore.hpp>

#include <client/params.hpp>

namespace csconnector {

struct Config {
  int port = 9090;
#ifdef AJAX_IFACE
  int ajax_port = 8081;
#endif
};

class connector {
public:
  connector(BlockChain& m_blockchain, slv2::SolverCore* solver, const Config& config = Config{});
  ~connector();

  connector(const connector&) = delete;
  connector& operator=(const connector&) = delete;

private:
  ::apache::thrift::stdcxx::shared_ptr<api::APIHandler> api_handler;
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
