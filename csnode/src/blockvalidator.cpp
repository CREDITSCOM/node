#include <csnode/blockvalidator.hpp>

#include <csnode/node.hpp>
#include <csnode/blockchain.hpp>
#include <csnode/walletsstate.hpp>

#include <csnode/blockvalidatorplugins.hpp>

namespace cs {

BlockValidator::BlockValidator(Node& node)
: node_(node)
, bc_(node_.getBlockChain())
, wallets_(::std::make_shared<WalletsState>(bc_.getCacheUpdater())) {
    plugins_.insert(std::make_pair(hashIntergrity, std::make_unique<HashValidator>(*this)));
    plugins_.insert(std::make_pair(blockNum, std::make_unique<BlockNumValidator>(*this)));
    plugins_.insert(std::make_pair(timestamp, std::make_unique<TimestampValidator>(*this)));
    plugins_.insert(std::make_pair(blockSignatures, std::make_unique<BlockSignaturesValidator>(*this)));
    plugins_.insert(std::make_pair(smartSignatures, std::make_unique<SmartSourceSignaturesValidator>(*this)));
    plugins_.insert(std::make_pair(balances, std::make_unique<BalanceChecker>(*this)));
    plugins_.insert(std::make_pair(transactionsSignatures, std::make_unique<TransactionsChecker>(*this)));
    plugins_.insert(std::make_pair(smartStates, std::make_unique<SmartStateValidator>(*this)));
    /*HL99dwfM3YPQnauN1djBvVLZNbC3b1FHwe5vPv8pDZ1y - 0xAAE*/
    /*CSa4DTfTcenryQAifiPKVpY9jzWshYY11g3mXQR6B7rJ - dAp*/
    /*8Vr9JA4AessnxVthGjp2ae7YLWQPU7jMvWYiPZA6vpDH - -253CS*/
    /*HtimoDtTYGSVotnQ5Eo4eud3FkDv5r2QYiKSZcdWP7Z8 - Timo*/
    /*Auh5VP1qJ8kQmWSzv7F6UEEExfBPmG39edxc9idRCfcR - zero balance after 4 trx*/
    plugins_.insert(std::make_pair(accountBalance, std::make_unique<AccountBalanceChecker>(*this, "Auh5VP1qJ8kQmWSzv7F6UEEExfBPmG39edxc9idRCfcR")));
    plugins_.insert(std::make_pair(balancesOnly, std::make_unique<BalanceOnlyChecker>(*this)));
}

BlockValidator::~BlockValidator() {}

inline bool BlockValidator::return_(ErrorType error, SeverityLevel severity) {
    return !(error >> severity);
}

bool BlockValidator::validateBlock(const csdb::Pool& block, ValidationFlags flags, SeverityLevel severity) {
    if (!flags || block.sequence() == 0ULL) {
        return true;
    }

    if (!block.is_valid()) {
        cserror() << "BlockValidator: invalid block received";
        return false;
    }

    if (!prevBlock_.is_valid() || block.sequence() - prevBlock_.sequence() != 1ULL) {
        prevBlock_ = bc_.loadBlock(block.previous_hash());
        if (!prevBlock_.is_valid()) {
            cserror() << "BlockValidator: block " << block.sequence() - 1ULL << " with hash " << block.previous_hash().to_string() << " is not valid.";
            return false;
        }
    }

    ErrorType validationResult = noError;
    for (auto& plugin : plugins_) {
        if (flags & plugin.first) {
            validationResult = plugin.second->validateBlock(block);
            if (!return_(validationResult, severity)) {
                return false;
            }
        }
    }

    prevBlock_ = block;
    return true;
}
}  // namespace cs
