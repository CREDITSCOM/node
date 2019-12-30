#include "pgbackend.h"

#include <dbsql/roundinfo.hpp>
#include <cscrypto/cscrypto.hpp>

namespace dbsql {
void saveConfidants(uint64_t round, const std::vector<cs::PublicKey>& confidants, uint64_t mask) {
  auto conn = PGBackend::instance().connection();

  int index = 0;
  for(auto& pkey: confidants) {
    auto pk58 = EncodeBase58(pkey.data(), pkey.data() + pkey.size());
    std::string trusted = mask & (1 << index++) ? "'t'" : "'f'";
    std::string query;
      query = "INSERT INTO public_keys(public_key)"
              "SELECT'" + pk58 + "'"
              "WHERE"
              "  NOT EXISTS ("
              "    SELECT public_key FROM public_keys WHERE public_key = '" + pk58 + "'"
              "  )";
      sendQuery(conn, query);

      query = "INSERT INTO round_info(round_num, public_id, real_trusted)"
                          "VALUES(" + std::to_string(round) +
                          ", (SELECT id from public_keys WHERE public_key='" + pk58 + "')"
                          ", " + trusted + ")";
      sendQuery(conn, query);
  }
  PGBackend::instance().freeConnection(conn);
}
}  // namespace dbsql
