#include "pgconnection.h"

#include <csnode/configholder.hpp>

namespace dbsql {
PGConnection::PGConnection() {
  auto conf = Config::get();

  connection_.reset(PQsetdbLogin(conf.dbhost.c_str(), std::to_string(conf.dbport).c_str(),
    nullptr, nullptr, conf.dbname.c_str(), conf.dbuser.c_str(), conf.dbpassword.c_str()), &PQfinish);

  if (PQstatus(connection_.get()) != CONNECTION_OK &&
    PQsetnonblocking(connection_.get(), 1) != 0) {
    throw std::runtime_error(PQerrorMessage(connection_.get()));
  }
}

std::shared_ptr<PGconn> PGConnection::connection() const {
  return connection_;
}

PGConnection::Config::Config() {
  auto csconfig = cs::ConfigHolder::instance().config()->getDbSQLData();

  dbhost = csconfig.host;
  dbport = csconfig.port;
  dbname = csconfig.name;
  dbuser = csconfig.user;
  dbpassword = csconfig.password;
}

PGConnection::Config& PGConnection::Config::get() {
  static PGConnection::Config instance;
  return instance;
}
}  // namespace dbsql
