#include <client/params.hpp>

#include <base58.h>

#include <cctype>
#include "apihandler.hpp"
#include "tokens.hpp"
#include "smartcontracts.hpp"

#ifdef TOKENS_CACHE

static inline bool isStringParam(const std::string& param) {
    return param.size() >= 2 && param[0] == '"' && param.back() == '"';
}

/*static inline bool isNormalTransfer(const std::string& method,
                                    const std::vector<std::string>& params) {
  return method == "transfer" && params.size() == 2 && isStringParam(params[0]) && isStringParam(params[1]);
}*/

static inline bool isNormalTransfer(const std::string& method, const std::vector<general::Variant>& params) {
    return method == "transfer" && params.size() == 2 && params[0].__isset.v_string &&
           params[1].__isset.v_string; /*&& isStringParam(params[0].v_string) && isStringParam(params[1].v_string);*/
}

/*static inline bool isTransferFrom(const std::string& method,
                                  const std::vector<std::string>& params) {
  return method == "transferFrom" && params.size() == 3 && isStringParam(params[0]) && isStringParam(params[1]) && isStringParam(params[1]);
}*/

static inline bool isTransferFrom(const std::string& method, const std::vector<general::Variant>& params) {
    return method == "transferFrom" && params.size() == 3 && params[0].__isset.v_string && params[1].__isset.v_string &&
           params[2].__isset.v_string; /*&& isStringParam(params[0].v_string) && isStringParam(params[1].v_string) && isStringParam(params[2].v_string);*/
}

static csdb::Address tryExtractPublicKey(const std::string& str) {
    // First try to decipher
    csdb::Address result;
    std::vector<uint8_t> vc;
    bool decodeSucc = DecodeBase58(str, vc);
    if (!decodeSucc || vc.size() != cscrypto::kPublicKeySize)
        return result;

    return csdb::Address::from_public_key(vc);
}

static csdb::Address tryGetRegisterData(const std::string& method, const std::vector<general::Variant>& params) {
    if (method == "register" && params.size() == 1)
        return tryExtractPublicKey(params[0].v_string);

    return csdb::Address();
}

static inline bool isValidAmountChar(const char c) {
    return std::isdigit(c) || c == '.' || c == ',';
}

static std::string tryExtractAmount(const std::string& str) {
    if (str.size() > 256)
        return "0";

    const auto end = str.end();
    bool hasSep = false;

    uint8_t leadingZeros = 0;   // From beginning
    uint8_t trailingZeros = 0;  // After .

    bool hasNonZero = false;

    std::string result;
    for (auto cIt = str.begin(); cIt != end; ++cIt) {
        if (!isValidAmountChar(*cIt)) {
            result.clear();
            break;
        }

        if (!std::isdigit(*cIt)) {  // Found a separator
            if (hasSep) {           // double separator
                result.clear();
                break;
            }

            hasSep = true;
            hasNonZero = true;
            result.push_back('.');
        }
        else {
            if (*cIt == '0') {
                if (!hasNonZero)
                    ++leadingZeros;
                else if (hasSep)
                    ++trailingZeros;
            }
            else {
                if (!hasSep)
                    hasNonZero = true;
                else
                    trailingZeros = 0;
            }

            result.push_back(*cIt);
        }
    }

    if (result.empty())
        return "0";
    result = result.substr(leadingZeros, result.size() - leadingZeros - trailingZeros);
    if (result.empty())
        return "0";

    if (result.front() == '.')
        result.insert(result.begin(), '0');
    if (result.back() == '.')
        result.pop_back();

    return result.empty() ? "0" : result;
}

static bool javaTypesEqual(const std::string& goodType, const std::string& questionType) {
    if (goodType.size() != questionType.size())
        return false;

    for (uint32_t i = 0; i < goodType.size(); ++i) {
        if (goodType[i] != static_cast<char>(std::tolower(questionType[i])))
            return false;
    }

    return true;
}

template <typename T>
T getVariantAs(const general::Variant&);
template <>
std::string getVariantAs(const general::Variant& var) {
    return var.v_string;
}

template <typename RetType>
void executeAndCall(api::APIHandler* p_api, const general::Address& addr, const general::Address& addr_smart, const std::vector<general::ByteCodeObject>& byteCodeObjects,
	const std::string& state, const std::string& method, const std::vector<general::Variant>& params, const std::function<void(const RetType&)> handler) {
	executor::ExecuteByteCodeResult result;

	if (byteCodeObjects.empty())
		return;
	std::vector<executor::MethodHeader> methodHeader;
	{
		executor::MethodHeader tmp;
		tmp.methodName = method;
		tmp.params = params;
		methodHeader.push_back(tmp);
	}
	p_api->getExecutor().executeByteCode(result, addr, addr_smart, byteCodeObjects, state, methodHeader, true /*isGetter*/, executor::Executor::kUseLastSequence);

	if (!result.status.code && !result.results.empty())
		handler(getVariantAs<RetType>(result.results[0].ret_val));
}

void TokensMaster::refreshTokenState(const csdb::Address& token, const std::string& newState, bool checkBalance) {
    bool present = false;
    auto byteCodeObjects = api_->getSmartByteCode(token, present);
    if (!present || byteCodeObjects.empty()) return;

    std::string name, symbol, totalSupply;
    csdb::Address deployer = tokens_[token].owner;

    general::Address addr   = std::string((char*)token.public_key().data(), token.public_key().size());
    general::Address dpAddr = std::string((char*)deployer.public_key().data(), deployer.public_key().size());

    executeAndCall<std::string>(api_, dpAddr, addr, byteCodeObjects, newState, "getName", std::vector<general::Variant>(),
                                [&name](const std::string& newName) { name = newName.substr(0, 255); });

    executeAndCall<std::string>(api_, dpAddr, addr, byteCodeObjects, newState, "totalSupply", std::vector<general::Variant>(),
        [&totalSupply](const std::string& newSupp) { totalSupply = tryExtractAmount(newSupp); });

    executeAndCall<std::string>(api_, dpAddr, addr, byteCodeObjects, newState, "getSymbol", std::vector<general::Variant>(),
        [&symbol](const std::string& newSymb) {
            symbol.clear();
            for (uint32_t i = 0; i < newSymb.size(); ++i) {
                if (i >= 4) break;
                symbol.push_back((char)std::toupper(newSymb[i]));
            }
        });  

    auto& t       = tokens_[token];
    t.name        = name;
    t.symbol      = symbol;
    t.totalSupply = totalSupply;

    // balance
    if (checkBalance) {
        executor::ExecuteByteCodeMultipleResult result;
        if (byteCodeObjects.empty())
            return;

        executor::SmartContractBinary smartContractBinary;
        smartContractBinary.contractAddress = addr;
        smartContractBinary.object.byteCodeObjects = byteCodeObjects;
        smartContractBinary.object.instance = newState;
        smartContractBinary.stateCanModify = 0;

        std::vector<csdb::Address> holders;
        holders.reserve(t.holders.size());
        for (auto& h : t.holders)
            holders.push_back(h.first);

        std::vector<std::vector<general::Variant>> holderKeysParams;
        holderKeysParams.reserve(holders.size());
        for (auto& h : holders) {
            general::Variant var;
            auto key = h.public_key();
            var.__set_v_string(EncodeBase58(cs::Bytes(key.begin(), key.end())));
            holderKeysParams.push_back(std::vector<general::Variant>(1, var));
        }

        api_->getExecutor().executeByteCodeMultiple(result, dpAddr, smartContractBinary, "balanceOf", holderKeysParams, 100, executor::Executor::kUseLastSequence);

        ++t.realHoldersCount = 0;
        if (!result.status.code && (result.results.size() == holders.size())) {
            for (uint32_t i = 0; i < holders.size(); ++i) {
                const auto& res = result.results[i];
                if (!res.status.code) {
                    t.holders[holders[i]].balance = tryExtractAmount(getVariantAs<std::string>(res.ret_val));
                    ++t.realHoldersCount;
                }
            }
        }
    }
}

/* Call under data lock only */
void TokensMaster::initiateHolder(Token& token, const csdb::Address& address, const csdb::Address& holder, bool increaseTransfers /* = false*/) {
    if (increaseTransfers)
        ++token.holders[holder].transfersCount;
    else
        token.holders[holder];

    holders_[holder].insert(address);
}

TokensMaster::TokensMaster(api::APIHandler* api)
: api_(api) {
}

TokensMaster::~TokensMaster() {
}

void TokensMaster::updateTokenChaches(const csdb::Address& addr, const std::string& newState, const TokenInvocationData::Params& ps) {
    csunused(newState);
    std::lock_guard<decltype(dataMut_)> lInt(dataMut_);
    auto tIt = tokens_.find(addr);
    if (tIt == tokens_.end())
        return;  // Ignore if not-a-token

    initiateHolder(tIt->second, tIt->first, ps.initiator);
    ++tIt->second.transactionsCount;

    if (!ps.method.empty()) {
        if (isTransfer(ps.method, ps.params)) {
            ++tIt->second.transfersCount;
            auto trPair = getTransferData(ps.initiator, ps.method, ps.params);
            if (trPair.first.is_valid())
                initiateHolder(tIt->second, tIt->first, trPair.first, true);
            if (trPair.second.is_valid())
                initiateHolder(tIt->second, tIt->first, trPair.second, true);
        }
        else if (tIt->second.tokenStandard == TokenStandard::CreditsExtended) {
            csdb::Address regDude = tryGetRegisterData(ps.method, ps.params);
            if (regDude.is_valid())
                initiateHolder(tIt->second, tIt->first, regDude);
        }
    }

    // Balance update   
    auto refreshBalance = [&](const csdb::Address& addrFrom, const csdb::Address& addrTo = csdb::Address{}, const std::string& amount = "") {
        auto getCurrBalance = [&](const csdb::Address& addrOwner) -> std::string {
            bool present = false;
            auto byteCodeObjects = api_->getSmartByteCode(addr, present);
            if (!present || byteCodeObjects.empty()) return "0";

            general::Address addrToken{ addr.public_key().begin(), addr.public_key().end() };

            auto dpAddrPK = tokens_[addr].owner.public_key();
            general::Address dpAddr{ dpAddrPK.begin(), dpAddrPK.end() };

            // param: owner
            std::vector<general::Variant> param(1);
            std::string addrOwnerStr = addrOwner.to_api_addr();
            param[0].__set_v_string(EncodeBase58({ addrOwnerStr.begin(), addrOwnerStr.end() }));
            //
            std::string retBalance{ "0" };
            executeAndCall<std::string>(api_, dpAddr, addrToken, byteCodeObjects, newState, "balanceOf", param,
                [&retBalance](const std::string& balance) { retBalance = balance; });

            if (!std::all_of(retBalance.begin(), retBalance.end(), [](char ch) { return (isdigit(ch) || ch == '.'); })) {
                csdebug() << "executor returns balance as " << retBalance;
                retBalance = "0";
            }
            return retBalance;
        };

        auto& t = tokens_[addr];
        if (addrTo == csdb::Address{}) { // for deploy token
            t.holders[addrFrom].balance = getCurrBalance(addrFrom);
            ++t.realHoldersCount;
            return;
        }

        // from
        auto& currFromBalance = t.holders[addrFrom].balance;
        auto newFromBalance = [&] {
            if (isZeroAmount(currFromBalance))
                return getCurrBalance(addrFrom);
            else
                return std::to_string(stof(currFromBalance) - stof(amount));
        }();
        if (isZeroAmount(newFromBalance))
            --t.realHoldersCount;
        currFromBalance = newFromBalance;

        // to
        auto& currToBalance = t.holders[addrTo].balance;
        auto newToBalance = [&] {
            if (isZeroAmount(currToBalance))
                return getCurrBalance(addrTo);
            else
                return std::to_string(stof(currToBalance) + stof(amount));
        }();
        if (isZeroAmount(currToBalance) && !isZeroAmount(newToBalance))
            ++t.realHoldersCount;
        currToBalance = newToBalance;
    };

    if(api_->isBDLoaded()){
        if (ps.method == "transfer" && ps.params.size() == 2)
            refreshBalance(ps.initiator, tryExtractPublicKey(ps.params[0].v_string), ps.params[1].v_string);
        else if (ps.method == "transferFrom" && ps.params.size() == 3)
            refreshBalance(tryExtractPublicKey(ps.params[0].v_string), tryExtractPublicKey(ps.params[1].v_string), ps.params[2].v_string);
        else if (ps.method.empty()) // deploy token
            refreshBalance(tokens_[addr].owner);

        refreshTokenState(addr, cs::SmartContracts::get_contract_state(api_->get_s_blockchain(), addr));
    }
}

void TokensMaster::checkNewDeploy(const csdb::Address& sc, const csdb::Address& deployer, const api::SmartContractInvocation& sci) {
    DeployTask dt;
    dt.address = sc;
    dt.deployer = deployer;
    dt.byteCodeObjects = sci.smartContractDeploy.byteCodeObjects;

    executor::GetContractMethodsResult methodsResult;
    if (!dt.byteCodeObjects.empty()) {
        api_->getExecutor().getContractMethods(methodsResult, dt.byteCodeObjects);
        if (!methodsResult.status.code) {
            if (methodsResult.tokenStandard != TokenStandard::NotAToken) {
                Token t;
                t.tokenStandard = methodsResult.tokenStandard;
                t.owner = dt.deployer;

                {
                    std::lock_guard<decltype(dataMut_)> lInt(dataMut_);
                    tokens_[dt.address] = t;
                }
            }
        }
    }
}

void TokensMaster::checkNewState(const csdb::Address& sc, const csdb::Address& initiator, const api::SmartContractInvocation& sci, const std::string& newState) {
    TokenInvocationData::Params ps;
    ps.initiator = initiator;
    ps.method = sci.method;
    ps.params = sci.params;
    updateTokenChaches(sc, newState, ps);
}

void TokensMaster::loadTokenInfo(const std::function<void(const TokensMap&, const HoldersMap&)> func) {
    std::lock_guard<decltype(dataMut_)> l(dataMut_);
    func(tokens_, holders_);
}

bool TokensMaster::isTransfer(const std::string& method, const std::vector<general::Variant>& params) {
    return isNormalTransfer(method, params) || isTransferFrom(method, params);
}

using AddrPair = std::pair<csdb::Address, csdb::Address>;

AddrPair TokensMaster::getTransferData(const csdb::Address& initiator, const std::string& method, const std::vector<general::Variant>& params) {
    AddrPair result;

    if (isNormalTransfer(method, params)) {
        result.first = initiator;
        result.second = tryExtractPublicKey(params[0].v_string);
    }
    else if (isTransferFrom(method, params)) {
        result.first = tryExtractPublicKey(params[0].v_string);
        result.second = tryExtractPublicKey(params[1].v_string);
    }

    return result;
}

std::string TokensMaster::getAmount(const api::SmartContractInvocation& sci) {
    if (isNormalTransfer(sci.method, sci.params))
        return tryExtractAmount(sci.params[1].v_string);
    else if (isTransferFrom(sci.method, sci.params))
        return tryExtractAmount(sci.params[2].v_string);

    return "0";
}

#else

TokensMaster::TokensMaster(api::APIHandler*) {
}
TokensMaster::~TokensMaster() {
}
void TokensMaster::checkNewDeploy(const csdb::Address&, const csdb::Address&, const api::SmartContractInvocation&) {
}
void TokensMaster::checkNewState(const csdb::Address&, const csdb::Address&, const api::SmartContractInvocation&, const std::string&) {
}
void TokensMaster::loadTokenInfo(const std::function<void(const TokensMap&, const HoldersMap&)>) {
}
bool TokensMaster::isTransfer(const std::string&, const std::vector<general::Variant>&) {
    return false;
}
std::pair<csdb::Address, csdb::Address> TokensMaster::getTransferData(const csdb::Address&, const std::string&, const std::vector<general::Variant>&) {
    return std::pair<csdb::Address, csdb::Address>();
}
std::string TokensMaster::getAmount(const api::SmartContractInvocation&) {
    return "";
}
#endif
