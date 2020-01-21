#ifndef ITER_VALIDATOR_HPP
#define ITER_VALIDATOR_HPP

#include <memory>
#include <set>
#include <vector>

#include <csnode/nodecore.hpp>
#include <csnode/transactionsvalidator.hpp>
#include <lib/system/common.hpp>

class BlockChain;

namespace csdb {
class AmountCommission;
}

namespace cs {
class SolverContext;
class WalletsState;
class SmartContracts;

class IterValidator {
public:
    using Transactions = std::vector<csdb::Transaction>;

    IterValidator(WalletsState& wallets);
    Characteristic formCharacteristic(SolverContext&, Transactions&, PacketsVector& smartsPackets);
    // transform characteristic from "array of reject reasons" to its canonical form of "0 or 1"
    void normalizeCharacteristic(Characteristic& inout) const;
    class SimpleValidator;

private:
    bool validateTransactions(SolverContext&, Bytes& characteristicMask, const Transactions&);

    void checkRejectedSmarts(SolverContext&, Bytes& characteristicMask, const Transactions&);

    void checkSignaturesSmartSource(SolverContext&, PacketsVector& smartContractsPackets);
    void checkTransactionsSignatures(SolverContext& context, const Transactions& transactions, Bytes& characteristicMask, PacketsVector& smartsPackets);
    bool checkTransactionSignature(SolverContext& context, const csdb::Transaction& transaction);

	Reject::Reason deployAdditionalCheck(SolverContext& context, size_t trxInd, const csdb::Transaction& transaction);

    std::unique_ptr<TransactionsValidator> pTransval_;
    std::set<csdb::Address> smartSourceInvalidSignatures_;
};

class IterValidator::SimpleValidator {
public:
    enum RejectCode : uint8_t {
        kAllCorrect = 0,
        kInsufficientBalance,
        kWrongSignature,
        kInsufficientMaxFee,
        kSourceDoesNotExists,
        kTooLarge,
        kContractViolation,
        kTransactionProhibited,
        kNoDelegate,
        kDifferentDelegatedAmount,
        kAmountTooLow
    };

    static std::string getRejectMessage(RejectCode);

    /// validates following parameters of transaction:
    /// 1. Signature
    /// 2. Amount of max fee, including both call to contract and transfer to payable contract
    /// 3. Presence of transaction's source in blockchain
    /// 4. Balance of transaction's source
    /// 5. 
    ///
    /// New states and smart source transactions will be banned, use full validation.
    static bool validate(const csdb::Transaction&, const BlockChain&, SmartContracts&,
                         csdb::AmountCommission* countedFee = nullptr, RejectCode* rc = nullptr);
};
}  // namespace cs
#endif  // ITER_VALIDATOR_HPP
