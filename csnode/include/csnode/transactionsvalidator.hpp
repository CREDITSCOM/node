#ifndef TRANSACTIONS_VALIDATOR_HPP
#define TRANSACTIONS_VALIDATOR_HPP

#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <csnode/walletsstate.hpp>
#include <csnode/eventreport.hpp>
#include <lib/system/common.hpp>
#include <limits>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace cs {

class SolverContext;

class TransactionsValidator {
public:
    using Transactions = std::vector<csdb::Transaction>;
    using CharacteristicMask = cs::Bytes;
    using TransactionIndex = WalletsState::TransactionIndex;
    using RejectedSmarts = std::vector<std::pair<csdb::Transaction, size_t>>;

public:
    struct Config {
        size_t initialNegNodesNum_ = 2 * 1024 * 1024;
    };

public:
    TransactionsValidator(WalletsState& walletsState, const Config& config);

    void reset(size_t transactionsNum);
    Reject::Reason validateTransaction(SolverContext& context, const Transactions& trxs, size_t trxInd);
    size_t checkRejectedSmarts(SolverContext& context, const Transactions& trxs, CharacteristicMask& maskIncluded);
    void validateByGraph(SolverContext& context, CharacteristicMask& maskIncluded, const Transactions& trxs);

    bool isRejectedSmart(const csdb::Address&) const;
	Reject::Reason getRejectReason(const csdb::Address&) const;
	auto& getValidNewStates() const {
        return validNewStates_;
    }

    void clearCaches();
    void saveNewState(const csdb::Address&, size_t blockIndex, Reject::Reason rejectReason);

    size_t getCntRemovedTrxsByGraph() const;
    bool duplicatedNewState(SolverContext&, const csdb::Address&) const;

private:
    using TrxList = std::vector<TransactionIndex>;
    using Node = WalletsState::WalletData;
    using Stack = std::vector<Node*>;
    static constexpr csdb::Amount zeroBalance_ = 0.0_c;

private:
	Reject::Reason validateTransactionAsSource(SolverContext& context, const Transactions& trxs, size_t trxInd);
	Reject::Reason validateNewStateAsSource(SolverContext& context, const csdb::Transaction& trx);
	Reject::Reason validateCommonAsSource(SolverContext& context, const Transactions& trxs, size_t trxInd, WalletsState::WalletData& wallState);

	Reject::Reason validateTransactionAsTarget(const csdb::Transaction& trx);

    void removeTransactions(SolverContext& context, Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded);
    bool removeTransactions_PositiveOne(SolverContext& context, Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded);
    bool removeTransactions_PositiveAll(SolverContext& context, Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded);
    bool removeTransactions_NegativeOne(SolverContext& context, Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded);
    bool removeTransactions_NegativeAll(SolverContext& context, Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded);

    size_t makeSmartsValid(SolverContext& context, RejectedSmarts& smarts, const csdb::Address& source, CharacteristicMask& maskIncluded);

private:
    Config config_;
    WalletsState& walletsState_;
    TrxList trxList_;
    std::map<csdb::Address, csdb::Amount> payableMaxFees_;
    std::unordered_map<csdb::Address, Reject::Reason> rejectedNewStates_;
    std::vector<std::pair<size_t, bool>> validNewStates_; // index in block + false if invalidated by smart source trx
    std::unordered_set<csdb::Address> duplicatedNewStates_;
    Stack negativeNodes_;
    size_t cntRemovedTrxs_;
};

inline void TransactionsValidator::clearCaches() {
    payableMaxFees_.clear();
    rejectedNewStates_.clear();
    validNewStates_.clear();
    duplicatedNewStates_.clear();
}

inline void TransactionsValidator::saveNewState(const csdb::Address& addr, size_t index, Reject::Reason rejectReason) {
    if (rejectReason != Reject::Reason::None) {
        rejectedNewStates_[addr] = rejectReason;
    }
    else {
        validNewStates_.push_back(std::make_pair(index, true));
    }
}

inline size_t TransactionsValidator::getCntRemovedTrxsByGraph() const {
    return cntRemovedTrxs_;
}

inline bool TransactionsValidator::isRejectedSmart(const csdb::Address& addr) const {
    return rejectedNewStates_.find(addr) != rejectedNewStates_.end();
}

inline Reject::Reason TransactionsValidator::getRejectReason(const csdb::Address& addr) const {
	auto item = rejectedNewStates_.find(addr);
	if (item != rejectedNewStates_.cend()) {
		return item->second;
	}
	return Reject::Reason::None;
}

}  // namespace cs
#endif  // TRANSACTIONS_VALIDATOR_HPP
