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
#include <solver/TransactionsValidator.h>

namespace slv2
{
    class SolverCore;
}

namespace cs {

class WalletsState;
class TransactionsValidator;
class Solver;

class Generals {
public:
  Generals(WalletsState& m_walletsState);
  ~Generals() = default;

  Generals(const Generals&) = delete;
  Generals& operator=(const Generals&) = delete;

  // obsolete:
  cs::Hash buildVector(const cs::TransactionsPacket& packet, cs::Solver* solver);
  cs::Hash buildVector(const cs::TransactionsPacket& packet, slv2::SolverCore* solver);

  void addVector(const HashVector& vector);
  void addMatrix(const HashMatrix& matrix, const cs::ConfidantsKeys& confidantNodes);

  // take desision
  uint8_t takeDecision(const cs::ConfidantsKeys& confidantNodes, const csdb::PoolHash& lasthash);
  uint8_t takeUrgentDecision(const size_t confNumber, const csdb::PoolHash &lasthash);
  static int8_t extractRaisedBitsCount(const csdb::Amount& amount);
  const HashMatrix& getMatrix() const;

  void addSenderToMatrix(uint8_t myConfNum);

  const PublicKey& getWriterPublicKey() const;

private:
  struct HashWeigth {
    cs::Hash hash;
    uint8_t weight = 0;

    inline HashWeigth() {
      hash.fill(0);
    }
  };

  cs::HashMatrix m_hMatrix;

  enum : unsigned int {
    UntrustedSize = 10000,
    TrustedSize = 100,
    TotalSize = 100
  };

  std::array<uint8_t, UntrustedSize> m_findUntrusted;
  std::array<uint8_t, TrustedSize> m_newTrusted;
  std::array<HashWeigth, TotalSize> m_hwTotal;

  PublicKey m_writerPublicKey;

  WalletsState& m_walletsState;
  std::unique_ptr<TransactionsValidator> m_transactionsValidator;
};
}  // namespace cs
#endif
