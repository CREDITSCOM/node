/**
*  @file fee.h
*  @author Sergey Sychev
*/

#ifndef SOLVER_FEE_H
#define SOLVER_FEE_H

#include <cstddef>

class Node;

namespace csdb {
class Pool;
}  // namespace csdb

namespace cs {
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
  *  @param node - is needed to have an access to blockchain.
  *  @param pool - counted fee will be set for each transaction
  *                in this pool.
  */
  void CountFeesInPool(Node* node, csdb::Pool* pool);

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
  void CountOneByteCost();
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
  void CountOneRoundCost();
  /** @brief Counts rounds frequency and save it in member rounds_frequency_.
  *
  *  Round frequency depends on time stamp difference between previous pool
  *  and pool of current round - 100.
  */
  void CountRoundsFrequency();
  double CountBlockTimeStampDifference(size_t num_block_from);
  inline void CountTotalTransactionsLength();
  inline void Init(Node* node, csdb::Pool* pool);

  size_t num_of_nodes_;
  size_t num_of_last_block_;
  size_t total_transactions_length_;
  double one_byte_cost_;
  double one_round_cost_;
  double rounds_frequency_;
  csdb::Pool* current_pool_;
  Node* node_;
};
}  // namespace Credits

#endif // SOLVER_FEE_H
