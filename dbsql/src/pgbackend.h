#ifndef PGBACKEND_H
#define PGBACKEND_H

#include <memory>
#include <mutex>
#include <queue>
#include <condition_variable>

#include <libpq-fe.h>

#include "pgconnection.h"

namespace dbsql {
class PGBackend {
public:
  PGBackend();
  std::shared_ptr<PGConnection> connection();
  void freeConnection(std::shared_ptr<PGConnection>);

  static PGBackend& instance();
private:
  void createPool();

  std::mutex mutex_;
  std::condition_variable condition_;
  std::queue<std::shared_ptr<PGConnection>> pool_;

  static constexpr int POOL = 10;
};

int sendQuery(std::shared_ptr<PGConnection> conn, std::string& query);
}  // namespace dbsql

#endif // PGBACKEND_H
