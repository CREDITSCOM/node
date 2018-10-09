////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef GENERALS_HPP
#define GENERALS_HPP

#include <blake2-impl.h>
#include <blake2.h>
#include <map>

#include <string>
#include <vector>

#include <csdb/csdb.h>
#include <csdb/pool.h>

#include <csnode/node.hpp>
#include <lib/system/keys.hpp>

namespace cs {

class Solver;

class Generals {
 public:
  Generals()  = default;
  ~Generals() = default;

  Generals(const Generals&) = delete;
  Generals& operator=(const Generals&) = delete;

  cs::Hash buildVector(cs::TransactionsPacket& packet, csdb::Pool& new_pool);

  void addVector(const HashVector& vector);
  void addMatrix(const HashMatrix& matrix, const cs::ConfidantsKeys& confidantNodes);

  // take desision
  uint8_t takeDecision(const cs::ConfidantsKeys& confidantNodes, const csdb::PoolHash& lasthash);
  static int8_t extractRaisedBitsCount(const csdb::Amount& amount);
  const HashMatrix& getMatrix() const;

  void addSenderToMatrix(uint8_t myConfNum);

  const Characteristic& getCharacteristic() const;
  const PublicKey& getWriterPublicKey() const;

 private:
  struct HashWeigth {
    char hash[HASH_LENGTH] = {};
    uint8_t weight = 0;
  };

  HashMatrix m_hMatrix;

  std::array<uint8_t, 10000> m_find_untrusted;
  std::array<uint8_t, 100> m_new_trusted;
  std::array<HashWeigth, 100> m_hw_total;

  Characteristic m_characteristic;
  PublicKey m_writerPublicKey;
};
}  // namespace cs
#endif
