#ifndef TOKENS_HPP
#define TOKENS_HPP

#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <queue>
#include <unordered_map>

#include <boost/functional/hash.hpp>
#include <csdb/address.hpp>

#include <ContractExecutor.h>

namespace api {
class APIHandler;
class SmartContractInvocation;
}

using TokenId = csdb::Address;
using HolderKey = csdb::Address;

enum TokenStandard {
    NotAToken = 0,
    CreditsBasic = 1,
    CreditsExtended = 2
};

struct Token {
    int64_t tokenStandard;
    csdb::Address owner;

    std::string name;
    std::string symbol;
    std::string totalSupply;

    uint64_t transactionsCount = 0;
    uint64_t transfersCount = 0;

    uint64_t realHoldersCount = 0;  // Non-zero balance

    struct HolderInfo {
        std::string balance = "0";
        uint64_t transfersCount = 0;
    };
    std::map<HolderKey, HolderInfo> holders;  // Including guys with zero balance
};

using TokensMap = std::unordered_map<TokenId, Token>;
using HoldersMap = std::unordered_map<HolderKey, std::set<TokenId>>;

class TokensMaster {
public:
    TokensMaster(api::APIHandler*);
    ~TokensMaster();

    void run();

    void checkNewDeploy(const csdb::Address& sc, const csdb::Address& deployer, const api::SmartContractInvocation&);

    void checkNewState(const csdb::Address& sc, const csdb::Address& initiator, const api::SmartContractInvocation&, const std::string& newState);

    void applyToInternal(const std::function<void(const TokensMap&, const HoldersMap&)>);

    static bool isTransfer(const std::string& method, const std::vector<general::Variant>& params);

    static std::pair<csdb::Address, csdb::Address> getTransferData(const csdb::Address& initiator, const std::string& method, const std::vector<general::Variant>& params);

    static std::string getAmount(const api::SmartContractInvocation&);

    static bool isZeroAmount(const std::string& str) {
        return str == "0";
    }

private:
    void refreshTokenState(const csdb::Address& token, const std::string& newState);

    void initiateHolder(Token&, const csdb::Address& token, const csdb::Address& holder, bool increaseTransfers = false);

    api::APIHandler* api_;

    std::mutex cvMut_;
    std::condition_variable tokCv_;

    struct DeployTask {
        csdb::Address address;
        csdb::Address deployer;
        std::vector<general::ByteCodeObject> byteCodeObjects;
    };
    std::queue<DeployTask> deployQueue_;

    struct TokenInvocationData {
        struct Params {
            csdb::Address initiator;
            std::string method;
            std::vector<general::Variant> params;
        };

        std::string newState;
        std::list<Params> invocations;
    };
    std::map<csdb::Address, TokenInvocationData> newExecutes_;

    std::mutex dataMut_;
    TokensMap tokens_;
    HoldersMap holders_;

    std::atomic<bool> running_ = {false};
    std::thread tokThread_;
};

#endif  // TOKENS_HPP
