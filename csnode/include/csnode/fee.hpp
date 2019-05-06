/**
 *  @file fee.hpp
 *  @author Sergey Sychev
 */

#ifndef SOLVER_FEE_HPP
#define SOLVER_FEE_HPP

#include <cstddef>
#include <map>
#include <vector>

#include <csdb/amount.hpp>
#include <csdb/transaction.hpp>
#include <lib/system/common.hpp>

class BlockChain;

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
    using Transactions = std::vector<csdb::Transaction>;

    /**
     * @brief Counts fees and sets them for each transaction.
     *
     * @param transactions - transactions from current pool.
     */
    csdb::Amount CountFeesInPool(const BlockChain& blockchain, Transactions& transactions, const Bytes& characteristicMask = {});

    /**
     * @brief Reset internal cache. Shout be called explicitly when restart node
     *        with not empty database.
     */
    void ResetTrustedCache(const BlockChain&);

    /**
     * @brief way to estimate weather max fee is enough before consensus
     * @return true if max fee >= potentialFee
     * @param potentialFee will contain possible amount of counted fee
     *        for transaction and caused events
     */
    static bool EstimateMaxFee(const csdb::Transaction&, csdb::Amount& potentialFee);

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
    void CountOneByteCost(const BlockChain& blockchain, Transactions& transactions, const Bytes& characteristicMask);

    /** @brief Set "counted_fee_" field for each transaction in transactions_.
     *
     *  To find counted fee it multiplies size of transaction by one_byte_cost_.
     */
    void SetCountedFee(Transactions& transactions, const Bytes& characteristicMask);

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
    void CountTotalTransactionsLength(Transactions& transactions, const Bytes& characteristicMask);
    size_t EstimateNumOfNodesInNetwork(const BlockChain& blockchain);
    void AddConfidants(const std::vector<cs::PublicKey>& pool);
    bool TakeDecisionOnCacheUpdate(const BlockChain& blockchain);

    size_t num_of_last_block_;
    size_t total_transactions_length_;
    double one_byte_cost_;
    double one_round_cost_;
    double rounds_frequency_;
    bool update_trusted_cache_;
    std::map<cs::PublicKey, uint64_t> last_trusted_;
};
}  // namespace cs
#endif  // SOLVER_FEE_HPP
