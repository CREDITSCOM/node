////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <blake2-impl.h>
#include <blake2.h>
#include <string>
#include <vector>
#include <map>


#include <csdb/csdb.h>
#include <csdb/pool.h>

#include <csnode/node.hpp>
#include <lib/system/keys.hpp>

namespace Credits {

class Solver;

class Generals {
 public:
  Generals();
  ~Generals();

  Generals(const Generals&) = delete;
  Generals& operator=(const Generals&) = delete;

  // Rewrite method//
  void chooseHeadAndTrusted(std::map<std::string, std::string>);
  void chooseHeadAndTrustedFake(std::vector<std::string>& hashes);

  Hash_ buildvector(csdb::Pool& _pool, csdb::Pool& new_pool);

  void addvector(HashVector vector);
  void addmatrix(HashMatrix matrix, const std::vector<PublicKey>& confidantNodes);

  // take desision
  uint8_t       take_decision(const std::vector<PublicKey>&, const uint8_t myConfNum, const csdb::PoolHash lasthash);
  static int8_t extractRaisedBitsCount(const csdb::Amount& amount);
  HashMatrix    getMatrix() const;

  void addSenderToMatrix(uint8_t myConfNum);
  void fake_block(std::string);

  std::vector<uint8_t> getCharacteristicMask() const;

 private:
  struct hash_weight {
    char    a_hash[HASH_LENGTH] = {};
    uint8_t a_weight = 0;
  };
  HashMatrix                   m_hMatrix;
  std::array<uint8_t, 10000>   m_find_untrusted;
  std::array<uint8_t, 100>     m_new_trusted;
  std::array<hash_weight, 100> m_hw_total;
  std::vector<uint8_t> m_characteristic_mask;
};
}
