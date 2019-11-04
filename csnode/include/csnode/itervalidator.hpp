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

class IterValidator {
public:
    using Transactions = std::vector<csdb::Transaction>;

    IterValidator(WalletsState& wallets);
    Characteristic formCharacteristic(SolverContext&, Transactions&, Packets& smartsPackets);

    class SimpleValidator;

private:
    bool validateTransactions(SolverContext&, Bytes& characteristicMask, const Transactions&);

    void checkRejectedSmarts(SolverContext&, Bytes& characteristicMask, const Transactions&);

    void checkSignaturesSmartSource(SolverContext&, Packets& smartContractsPackets);
    void checkTransactionsSignatures(SolverContext& context, const Transactions& transactions, Bytes& characteristicMask, Packets& smartsPackets);
    bool checkTransactionSignature(SolverContext& context, const csdb::Transaction& transaction);

    bool deployAdditionalCheck(SolverContext& context, size_t trxInd, const csdb::Transaction& transaction);

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
        kSourceDoesNotExists
    };

    static std::string getRejectMessage(RejectCode);

    /// validates following parameters of transaction:
    /// 1. Signature
    /// 2. Amount of max fee
    /// 3. Presence of transaction's source in blockchain
    /// 4. Balance of transaction's source
    ///
    /// New states and smart source transactions will be banned, use full validation.
    static bool validate(const csdb::Transaction&, const BlockChain&,
                         csdb::AmountCommission* countedFee = nullptr, RejectCode* rc = nullptr);
};
}  // namespace cs
#endif  // ITER_VALIDATOR_HPP
