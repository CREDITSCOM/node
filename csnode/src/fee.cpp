/**
 *  @file fee.cpp
 *  @author Sergey Sychev
 */

#include "csnode/fee.hpp"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <limits>
#include <string>

#include <csdb/amount_commission.hpp>
#include <csdb/transaction.hpp>
#include <csnode/blockchain.hpp>

namespace cs {
namespace {
constexpr auto kMaxRoundNumWithFixedFee = 10;
constexpr auto kLengthOfCommonTransaction = 100;
constexpr double kMarketRateCS = 0.06;
constexpr double kFixedOneByteFee = 0.001 / kMarketRateCS / kLengthOfCommonTransaction;
constexpr double kNodeRentalCostPerDay = 100. / 30.5 / kMarketRateCS;
constexpr size_t kNumOfBlocksToCountFrequency = 100;
constexpr size_t kBlocksNumForNodesQtyEstimation = 100;
constexpr double kDefaultRoundFrequency = 3.0;
// constexpr size_t kDefaultRoundsPerDay = 24 * 3600 * kDefaultRoundFrequency;
constexpr uint8_t kInvalidMarker = 0;
constexpr uint8_t kValidMarker = 1;
}  // namespace

Fee::Fee()
: num_of_last_block_(0)
, total_transactions_length_(0)
, one_byte_cost_(0)
, one_round_cost_(0)
, rounds_frequency_(kDefaultRoundFrequency)
, update_trusted_cache_(true) {
}

csdb::Amount Fee::CountFeesInPool(const BlockChain& blockchain, Transactions& transactions, const Bytes& characteristicMask) {
    update_trusted_cache_ = TakeDecisionOnCacheUpdate(blockchain);
    num_of_last_block_ = blockchain.getLastSequence();

    if (transactions.empty()) {
        EstimateNumOfNodesInNetwork(blockchain);
        return csdb::Amount(0);
    }
    CountOneByteCost(blockchain, transactions, characteristicMask);
    SetCountedFee(transactions, characteristicMask);
    return csdb::Amount(one_round_cost_);
}

bool Fee::TakeDecisionOnCacheUpdate(const BlockChain& blockchain) {
    if (num_of_last_block_ > blockchain.getLastSequence()) {
        ResetTrustedCache(blockchain);
        return false;
    }
    else if (num_of_last_block_ == blockchain.getLastSequence() && last_trusted_.size() != 0) {
        return false;
    }
    else {
        return true;
    }
}

void Fee::SetCountedFee(Transactions& transactions, const Bytes& characteristicMask) {
    const size_t maskSize = characteristicMask.size();
    size_t i = 0;
    for (auto& transaction : transactions) {
        if (maskSize != 0) {
            if (i < maskSize && characteristicMask[i] != kValidMarker) {
                continue;
            }
        }
        size_t size_of_transaction = transaction.to_byte_stream().size();
        double counted_fee = one_byte_cost_ * size_of_transaction;
        double max_comission = kFixedOneByteFee * size_of_transaction;
        if (counted_fee > max_comission) {
            csdebug() << "Fee> counted_fee " << counted_fee << " is restricted with max_comission value " << max_comission;
            counted_fee = max_comission;
        }
        transaction.set_counted_fee(csdb::AmountCommission(std::max(kMinFee, counted_fee)));
        ++i;
    }
}

void Fee::CountOneByteCost(const BlockChain& blockchain, Transactions& transactions, const Bytes& characteristicMask) {
    if (num_of_last_block_ == 0) {
        one_byte_cost_ = 0;
        return;
    }

    if (num_of_last_block_ <= kMaxRoundNumWithFixedFee) {
        one_byte_cost_ = kFixedOneByteFee;
        EstimateNumOfNodesInNetwork(blockchain);
        return;
    }

    CountTotalTransactionsLength(transactions, characteristicMask);
    CountOneRoundCost(blockchain);
    one_byte_cost_ = one_round_cost_ / total_transactions_length_;
}

void Fee::CountTotalTransactionsLength(Transactions& transactions, const Bytes& characteristicMask) {
    total_transactions_length_ = 0;
    auto maskSize = characteristicMask.size();
    if (!maskSize) {
        for (const auto& transaction : transactions) {
            total_transactions_length_ += transaction.to_byte_stream().size();
        }
    }
    else {
        assert(transactions.size() == maskSize);
        for (size_t i = 0; i < transactions.size(); ++i) {
            if (i < maskSize && characteristicMask[i] == kValidMarker) {
                total_transactions_length_ += transactions[i].to_byte_stream().size();
            }
        }
    }
}

void Fee::CountOneRoundCost(const BlockChain& blockchain) {
    CountRoundsFrequency(blockchain);
    double num_of_rounds_per_day = 3600.0 * 24.0 * rounds_frequency_;
    if (num_of_rounds_per_day < 1) {
        num_of_rounds_per_day = 1;
    }
    one_round_cost_ = (kNodeRentalCostPerDay * EstimateNumOfNodesInNetwork(blockchain)) / num_of_rounds_per_day;
}

size_t Fee::EstimateNumOfNodesInNetwork(const BlockChain& blockchain) {
    if (!update_trusted_cache_) {
        return last_trusted_.size();
    }
    csmeta(csdebug) << "begin";
    csdb::Pool pool = blockchain.loadBlock(num_of_last_block_);

    if (!pool.is_valid()) {
        cserror() << "Fee> Pool is not valid. Error load block: " << num_of_last_block_;
        return last_trusted_.size();
    }

    AddConfidants(pool.confidants());

    if (num_of_last_block_ <= kBlocksNumForNodesQtyEstimation) {
        return last_trusted_.size();
    }

    const auto sequence_to_remove = num_of_last_block_ - kBlocksNumForNodesQtyEstimation;
    csdb::Pool pool_to_remove_conf = blockchain.loadBlock(sequence_to_remove);
    if (!pool_to_remove_conf.is_valid()) {
        cserror() << "Fee> Pool is not valid. Error load block for remove confidants: " << sequence_to_remove;
        return last_trusted_.size();
    }

    const auto& confidants_to_remove = pool_to_remove_conf.confidants();
    for (const auto& conf : confidants_to_remove) {
        auto it = last_trusted_.find(conf);
        if (it != last_trusted_.end()) {
            --(it->second);
            if (it->second == 0) {
                last_trusted_.erase(it);
            }
        }
        else {
            cserror() << "Fee> Confidants to remove is not contained to last trusted confidants.";
            cserror() << "Fee> Confidant: " << cs::Utils::byteStreamToHex(conf.data(), conf.size()) << ", Round: " << sequence_to_remove;
        }
    }

    return last_trusted_.size();
}

void Fee::CountRoundsFrequency(const BlockChain& blockchain) {
    if (num_of_last_block_ <= kMaxRoundNumWithFixedFee) {
        return;
    }

    size_t block_number_from;
    if (num_of_last_block_ > kNumOfBlocksToCountFrequency) {
        block_number_from = num_of_last_block_ - kNumOfBlocksToCountFrequency;
    }
    else {
        block_number_from = 1;
    }

    double time_stamp_diff = CountBlockTimeStampDifference(block_number_from, blockchain);
    if (time_stamp_diff > std::numeric_limits<double>::epsilon()) {
        rounds_frequency_ = (num_of_last_block_ - block_number_from + 1) * 1000.0 / time_stamp_diff;
    }
    else {
        rounds_frequency_ = kDefaultRoundFrequency;
    }
}

double Fee::CountBlockTimeStampDifference(size_t num_block_from, const BlockChain& blockchain) {
    csdb::Pool block_from = blockchain.loadBlock(num_block_from);
    if (!block_from.is_valid()) {
        csmeta(cserror) << "load block: " << num_block_from << " is not valid";
        return 0;
    }
    double time_stamp_from = std::stod(block_from.user_field(0).value<std::string>());

    csdb::PoolHash block_to_hash = blockchain.getLastHash();
    csdb::Pool block_to = blockchain.loadBlock(block_to_hash);
    double time_stamp_to = time_stamp_from;
    const auto str = block_to.user_field(0).value<std::string>();
    if (!str.empty()) {
        try {
            time_stamp_to = std::stod(str);
        }
        catch (...) {
        }
    }

    return time_stamp_to - time_stamp_from;
}

void Fee::ResetTrustedCache(const BlockChain& blockchain) {
    last_trusted_.clear();
    size_t last_sequence = blockchain.getLastSequence();
    num_of_last_block_ = last_sequence;
    if (last_sequence == 0) {
        return;
    }

    const size_t sequence_to_stop = (last_sequence > kBlocksNumForNodesQtyEstimation ? last_sequence - kBlocksNumForNodesQtyEstimation : 0);

    while (last_sequence > sequence_to_stop) {
        csdb::Pool pool = blockchain.loadBlock(last_sequence);
        if (!pool.is_valid()) {
            break;
        }

        AddConfidants(pool.confidants());

        --last_sequence;
    }
}

void Fee::AddConfidants(const std::vector<cs::PublicKey>& confidants) {
    for (const auto& conf : confidants) {
        auto it = last_trusted_.find(conf);
        if (it != last_trusted_.end()) {
            ++(it->second);
        }
        else {
            last_trusted_.emplace(std::make_pair(conf, 1));
        }
    }
}
}  // namespace cs
