/**
 *  @file fee.hpp
 *  @author Sergey Sychev
 */

#ifndef SOLVER_FEE_HPP
#define SOLVER_FEE_HPP

#include <cstddef>
#include <map>

#include <lib/system/common.hpp>

class BlockChain;

namespace csdb {
class Pool;
}  // namespace csdb

namespace cs {
class TransactionsPacket;

/** @brief This class was designed to count round fee.
 *
 *  The amount of counted fee in round depends on:
 *  - quantity of nodes in the network
 *  - number of transaction in round
 *  - size of transaction
 *  - cost of one node
 */

class Fee {
public:
  /** @brief Counts fee for each transaction in pool.
  *
  *  @param pool/packet - counted fee will be set for each transaction
  *                       in this pool/packet.
  */
  void CountFeesInPool(const BlockChain& blockchain, csdb::Pool* pool);
  void CountFeesInPool(const BlockChain& blockchain, TransactionsPacket* packet);

  Fee();
  Fee(const Fee&) = delete;
  const Fee& operator=(const Fee&) = delete;

private:
  /** @brief Counts cost of one byte in transaction in current round.
   *
   *  To count full amount of fee, result of this function (stored in
   *  one_byte_cost_ member) should be multiplied by number of bytes
   *  in transaction.
   */
  void CountOneByteCost(const BlockChain& blockchain);
  /** @brief Set "counted_fee_" field for each transaction in current_pool_.
   *
   *  To find counted fee it multiplies size of transaction by one_byte_cost_.
   */
  void SetCountedFee();
  /** @brief Counts cost of the current round.
   *
   *  Cost of round depends on number of rounds per day, number of nodes in
   *  the network and rental cost of one node per day.
   *  Sets one_round_cost_.
   */
  void CountOneRoundCost(const BlockChain& blockchain);
  /** @brief Counts rounds frequency and save it in member rounds_frequency_.
   *
   *  Round frequency depends on time stamp difference between previous pool
   *  and pool of current round - 100.
   */
  void CountRoundsFrequency(const BlockChain& blockchain);
  double CountBlockTimeStampDifference(size_t num_block_from, const BlockChain& blockchain);
  void CountTotalTransactionsLength();
  size_t EstimateNumOfNodesInNetwork(const BlockChain& blockchain);
  inline void Init(const BlockChain& blockchain, csdb::Pool* pool);
  inline void Init(const BlockChain&, TransactionsPacket* packet);
  void ResetTrustedCache(const BlockChain&);
  void AddConfidants(const std::vector<cs::PublicKey>& pool);

  size_t num_of_last_block_;
  size_t total_transactions_length_;
  double one_byte_cost_;
  double one_round_cost_;
  double rounds_frequency_;
  bool update_trusted_cache_;
  csdb::Pool* current_pool_;
  TransactionsPacket* transactions_packet_;
  std::map<cs::PublicKey, uint64_t> last_trusted_;
};
}  // namespace cs

#endif  // SOLVER_FEE_HPP
