#ifndef CSCONNCETOR_HPP
#define CSCONNCETOR_HPP

#if defined(_MSC_VER)
#pragma warning(push)
// 4245: 'return': conversion from 'int' to 'SOCKET', signed/unsigned mismatch
#pragma warning(disable: 4245)
#endif

#include <apihandler.hpp>

#include <thrift/server/TThreadPoolServer.h>
#include <thrift/server/TThreadedServer.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <solvercore.hpp>
#include <client/params.hpp>

#include <memory>
#include <thread>

namespace csconnector {

struct Config {
  int port = 9090;
#ifdef AJAX_IFACE
  int ajax_port = 8081;
#endif
  int executor_port = 9080;
  int apiexec_port = 9070;
};

class connector {
public:
  using ApiHandlerPtr = ::apache::thrift::stdcxx::shared_ptr<api::APIHandler>;
  using ApiExecHandlerPtr = ::apache::thrift::stdcxx::shared_ptr<apiexec::APIEXECHandler>;

  explicit connector(BlockChain& m_blockchain, cs::SolverCore* solver, const Config& config = Config{});
  ~connector();

  connector(const connector&) = delete;
  connector& operator=(const connector&) = delete;

  // interface
  ApiHandlerPtr apiHandler() const;
  ApiExecHandlerPtr apiExecHandler() const;

private:
  executor::Executor& executor_;
  ApiHandlerPtr api_handler;
  ApiExecHandlerPtr apiexec_handler;
  ::apache::thrift::stdcxx::shared_ptr<::api::APIProcessor> p_api_processor;
  ::apache::thrift::stdcxx::shared_ptr<::apiexec::APIEXECProcessor> p_apiexec_processor;
#ifdef BINARY_TCP_API
  ::apache::thrift::server::TThreadedServer server;
  std::thread thread;
#endif
#ifdef AJAX_IFACE
  ::apache::thrift::server::TThreadedServer ajax_server;
  std::thread ajax_thread;
#endif
#ifdef BINARY_TCP_EXECAPI
  ::apache::thrift::server::TThreadedServer exec_server;
  std::thread exec_thread;
#endif
};
}  // namespace csconnector

#endif  // CSCONNCETOR_HPP
