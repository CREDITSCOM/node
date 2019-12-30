#ifndef PGCONNECTION_H
#define PGCONNECTION_H

#include <memory>
#include <mutex>
#include <string>

#include <libpq-fe.h>

namespace dbsql {
class PGConnection {
public:
  PGConnection();

  std::shared_ptr<PGconn> connection() const;
private:
  void establish_connection();

  struct Config {
    Config();

    std::string dbhost;
    int         dbport;
    std::string dbname;
    std::string dbuser;
    std::string dbpassword;

    static Config& get();
  };

  std::shared_ptr<PGconn>  connection_;
};
}  // namespace dbsql

#endif // PGCONNECTION_H
