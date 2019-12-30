#include "pgbackend.h"

#include <iostream>
#include <thread>
#include <fstream>
#include <sstream>

namespace dbsql {
PGBackend::PGBackend() {
  createPool();
}

void PGBackend::createPool() {
  std::lock_guard<std::mutex> lock_(mutex_);

  for (auto i = 0; i < POOL; ++i) {
     pool_.emplace(std::make_shared<PGConnection>());
  }
}

std::shared_ptr<PGConnection> PGBackend::connection() {
  std::unique_lock<std::mutex> lock_(mutex_);

  while (pool_.empty()) {
    condition_.wait(lock_);
  }

  auto conn_ = pool_.front();
  pool_.pop();

  return  conn_;
}

void PGBackend::freeConnection(std::shared_ptr<PGConnection> conn_) {
  std::unique_lock<std::mutex> lock_(mutex_);

  pool_.push(conn_);
  lock_.unlock();
  condition_.notify_one();
}

int sendQuery(std::shared_ptr<PGConnection> conn, std::string& query) {
  PQsendQuery(conn->connection().get(), query.c_str());
  while (auto res = PQgetResult(conn->connection().get())) {
    PQclear(res);
  }
  return 0;
}

PGBackend& PGBackend::instance() {
  static PGBackend instance;
  return instance;
}
}  // namespacce bdsql
