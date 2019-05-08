#ifndef SOLVER_FEE_HPP
#define SOLVER_FEE_HPP

#include <cstddef>
#include <vector>

#include <csdb/amount_commission.hpp>
#include <csdb/transaction.hpp>

namespace cs {
namespace fee {

using Transactions = std::vector<csdb::Transaction>;

///
/// @brief sets counted fee for each transaction in passed container
///
void setCountedFees(Transactions&);

///
/// @brief allows to estimate weather max fee is enough before consensus
/// @return true if max fee >= countedFee
///
bool estimateMaxFee(const csdb::Transaction&, csdb::AmountCommission& countedFee);

///
/// @return counted fee for transaction
///
csdb::AmountCommission getFee(const csdb::Transaction&);

} // namespace fee
} // namespace cs
#endif  // SOLVER_FEE_HPP
