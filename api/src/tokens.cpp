#include <client/params.hpp>

#include <base58.h>

#include "apihandler.hpp"
#include "tokens.hpp"
#include <cctype>

#ifdef TOKENS_CACHE

static inline bool isStringParam(const std::string& param) {
  return param.size() >= 2 && param[0] == '"' && param.back() == '"';
}

/*static inline bool isNormalTransfer(const std::string& method,
                                    const std::vector<std::string>& params) {
  return method == "transfer" && params.size() == 2 && isStringParam(params[0]) && isStringParam(params[1]);
}*/

static inline bool isNormalTransfer(const std::string& method,
  const std::vector<general::Variant>& params) {
  return method == "transfer" && params.size() == 2 && params[0].__isset.v_string && params[1].__isset.v_string; /*&& isStringParam(params[0].v_string) && isStringParam(params[1].v_string);*/
}

/*static inline bool isTransferFrom(const std::string& method,
                                  const std::vector<std::string>& params) {
  return method == "transferFrom" && params.size() == 3 && isStringParam(params[0]) && isStringParam(params[1]) && isStringParam(params[1]);
}*/

static inline bool isTransferFrom(const std::string& method,
  const std::vector<general::Variant>& params) {
  return method == "transferFrom" && params.size() == 3 && params[0].__isset.v_string && params[1].__isset.v_string && params[2].__isset.v_string; /*&& isStringParam(params[0].v_string) && isStringParam(params[1].v_string) && isStringParam(params[2].v_string);*/
}

static csdb::Address tryExtractPublicKey(const std::string& str) {
  // First try to decipher
  csdb::Address result;
  std::vector<uint8_t> vc;
  bool decodeSucc = DecodeBase58(str, vc);
  if (!decodeSucc || vc.size() != PUBLIC_KEY_LENGTH) return result;

  return csdb::Address::from_public_key(vc);
}

static csdb::Address tryGetRegisterData(const std::string& method,
  const std::vector<general::Variant>& params) {
  if (method == "register" && params.size() == 1)
    return tryExtractPublicKey(params[0].v_string);

  return csdb::Address();
}

static inline bool isValidAmountChar(const char c) {
  return std::isdigit(c) || c == '.' || c == ',';
}

static std::string tryExtractAmount(const std::string& str) {
  if (str.size() > 256) return "0";

  const auto end = str.end();
  bool hasSep = false;

  uint8_t leadingZeros = 0; // From beginning
  uint8_t trailingZeros = 0; // After .

  bool hasNonZero = false;

  std::string result;
  for (auto cIt = str.begin(); cIt != end; ++cIt) {
    if (!isValidAmountChar(*cIt)) {
      result.clear();
      break;
    }

    if (!std::isdigit(*cIt)) {  // Found a separator
      if (hasSep) {  // double separator
        result.clear();
        break;
      }

      hasSep = true;
      hasNonZero = true;
      result.push_back('.');
    }
    else {
      if (*cIt == '0') {
        if (!hasNonZero) ++leadingZeros;
        else if (hasSep) ++trailingZeros;
      }
      else {
        if (!hasSep) hasNonZero = true;
        else trailingZeros = 0;
      }

      result.push_back(*cIt);
    }
  }

  if (result.empty()) return "0";
  result = result.substr(leadingZeros, result.size() - leadingZeros - trailingZeros);
  if (result.empty()) return "0";

  if (result.front() == '.') result.insert(result.begin(), '0');
  if (result.back() == '.') result.pop_back();

  return result.empty() ? "0" : result;
}

static bool javaTypesEqual(const std::string& goodType, const std::string& questionType) {
  if (goodType.size() != questionType.size()) return false;

  for (uint32_t i = 0; i < goodType.size(); ++i) {
    if (goodType[i] != static_cast<char>(std::tolower(questionType[i])))
      return false;
  }

  return true;
}

TokenStandart TokensMaster::getTokenStandart(const std::vector<general::MethodDescription>& methods) {
  const static std::map<std::string, std::pair<std::string, std::vector<std::string>>>
    StandartMethods =
    {
     { "getName", { "java.lang.string", { } } },
     { "getSymbol", { "java.lang.string", { } } },
     { "getDecimal", { "int", { } } },
     { "setFrozen", { "boolean", { "boolean" } } },
     { "totalSupply", { "java.lang.string", { } } },
     { "balanceOf", { "java.lang.string", { "java.lang.string" } } },
     { "allowance", { "java.lang.string", { "java.lang.string", "java.lang.string" } } },
     { "transfer", { "boolean", { "java.lang.string", "java.lang.string" } } },
     { "transferFrom", { "boolean", { "java.lang.string", "java.lang.string", "java.lang.string" } } },
     { "approve", { "void", { "java.lang.string", "java.lang.string" } } },
     { "burn", { "boolean", { "java.lang.string" } } },
     { "register", { "void", { } } },
     { "buyTokens", { "boolean", { "java.lang.string" } } }
    };

  const static auto findPos = [](const std::string& key) { return std::distance(StandartMethods.begin(), StandartMethods.find(key)); };
  const static std::pair<size_t, size_t> extendedPositions = { findPos("register"), findPos("buyTokens") };

  std::vector<bool> gotMethods(StandartMethods.size(), false);
  for (auto& m : methods) {
    auto smIt = StandartMethods.find(m.name);

    if (smIt != StandartMethods.end()) {
      if (smIt->second.second.size() != m.arguments.size() ||
          !javaTypesEqual(smIt->second.first, m.returnType)) continue;

      bool argsMatch = true;
      for (uint32_t i = 0; i < m.arguments.size(); ++i) {
        if (!javaTypesEqual(smIt->second.second[i], m.arguments[i].type)) {
          argsMatch = false;
          break;
        }
      }

      if (argsMatch)
        gotMethods[std::distance(StandartMethods.begin(), smIt)] = true;
    }
  }

  bool canBeCredits = true;
  bool canBeCreditsExtended = true;
  for (uint8_t i = 0; i < gotMethods.size(); ++i) {
    if (!gotMethods[i]) {
      canBeCreditsExtended = false;
      if (i != extendedPositions.first && i != extendedPositions.second) {
        canBeCredits = false;
        break;
      }
    }
  }

  return canBeCreditsExtended ? TokenStandart::CreditsExtended :
    (canBeCredits ? TokenStandart::CreditsBasic : TokenStandart::NotAToken);
}

template <typename T> T getVariantAs(const general::Variant&);
template <> std::string getVariantAs(const general::Variant& var) { return var.v_string; }

template <typename RetType>
void executeAndCall(executor::ContractExecutorConcurrentClient& executor,
                    const api::Address& addr,
                    const std::vector<general::ByteCodeObject> &byteCodeObjects,
                    const std::string& state,
                    const std::string& method,
                    const std::vector<general::Variant>& params,
                    const uint32_t timeout,
                    const std::function<void(const RetType&)> handler) {
  executor::ExecuteByteCodeResult result;

  executor.executeByteCode(result,
                           addr,
                           byteCodeObjects,
                           state,
                           method,
                           params,
                           timeout);

  if (!result.status.code) handler(getVariantAs<RetType>(result.ret_val));
}

void TokensMaster::refreshTokenState(const csdb::Address& token,
                                     const std::string& newState) {
  bool present = false;
  auto byteCodeObjects = api_->getSmartByteCode(token, present);
  if (!present) return;

  const auto pk = token.public_key();
  api::Address addr = std::string((char*)pk.data(), pk.size());

  std::string name, symbol, totalSupply;

  executeAndCall<std::string>(api_->getExecutor(), addr, byteCodeObjects, newState,
                 "getName", std::vector<general::Variant>(), 250,
                 [&name](const std::string& newName) {
                   name = newName.substr(0, 255);
                 });

  executeAndCall<std::string>(api_->getExecutor(), addr, byteCodeObjects, newState,
                 "getSymbol", std::vector<general::Variant>(), 250,
                 [&symbol](const std::string& newSymb) {
                   symbol.clear();

                   for (uint32_t i = 0; i < newSymb.size(); ++i) {
                     if (i >= 4) break;
                     symbol.push_back((char)std::toupper(newSymb[i]));
                   }
                 });

  executeAndCall<std::string>(api_->getExecutor(), addr, byteCodeObjects, newState,
                 "totalSupply", std::vector<general::Variant>(), 250,
                 [&totalSupply](const std::string& newSupp) {
                   totalSupply = tryExtractAmount(newSupp);
                 });

  std::vector<csdb::Address> holders;

  csdb::Address deployer;
  {
    std::lock_guard<decltype(dataMut_)> l(dataMut_);
    auto& t = tokens_[token];
    t.name = name;
    t.symbol = symbol;
    t.totalSupply = totalSupply;

    deployer = t.owner;

    holders.reserve(t.holders.size());
    for (auto& h : t.holders) holders.push_back(h.first);
  }

  const auto dp = deployer.public_key();
  api::Address dpAddr = std::string((char*)dp.data(), dp.size());

  std::vector<std::vector<general::Variant>> holderKeysParams;
  holderKeysParams.reserve(holders.size());
  for (auto& h : holders) {
    general::Variant var;
    //var.__set_v_string('"' + EncodeBase58(h.public_key()) + '"');
    auto key = h.public_key();
    cs::Bytes pk(key.begin(), key.end());
    var.__set_v_string(EncodeBase58(pk));
    holderKeysParams.push_back(std::vector<general::Variant>(1, var));
  }

  executor::ExecuteByteCodeMultipleResult result;
  api_->getExecutor().
    executeByteCodeMultiple(result, dpAddr, byteCodeObjects, newState,
                            "balanceOf", holderKeysParams, 100);

  if (!result.status.code &&
      (result.results.size() == holders.size())) {
    std::lock_guard<decltype(dataMut_)> l(dataMut_);
    auto& t = tokens_[token];

    for (uint32_t i = 0; i < holders.size(); ++i) {
      const auto& res = result.results[i];
      if (!res.status.code) {
        auto& oldBalance = t.holders[holders[i]].balance;
        //auto newBalance = tryExtractAmount('"' + getVariantAs<std::string>(res.ret_val) + '"');
        auto newBalance = tryExtractAmount(getVariantAs<std::string>(res.ret_val));
        if (isZeroAmount(newBalance) && !isZeroAmount(oldBalance)) --t.realHoldersCount;
        else if (isZeroAmount(oldBalance) && !isZeroAmount(newBalance)) ++t.realHoldersCount;
        oldBalance = newBalance;
      }
    }
  }
}

/* Call under data lock only */
void TokensMaster::initiateHolder(Token& token,
                                  const csdb::Address& address,
                                  const csdb::Address& holder,
                                  bool increaseTransfers/* = false*/) {
  if (increaseTransfers)
    ++token.holders[holder].transfersCount;
  else
    token.holders[holder];

  holders_[holder].insert(address);
}

TokensMaster::TokensMaster(api::APIHandler* api): api_(api) { }

TokensMaster::~TokensMaster() {
  running_.store(false);

  if (tokThread_.joinable()) {
    tokCv_.notify_all();
    tokThread_.join();
  }
}

void TokensMaster::run() {
  running_.store(true);

  tokThread_ = std::thread([this]() {
    while (running_.load()) {
      std::unique_lock<std::mutex> l(cvMut_);
      while (!deployQueue_.empty()) {
        DeployTask dt = std::move(deployQueue_.front());
        deployQueue_.pop();
        l.unlock();

        executor::GetContractMethodsResult methodsResult;

        try { api_->getExecutor(); }
        catch (...) { std::cout << "executor dosent run!" << std::endl; return; }

        api_->getExecutor().getContractMethods(methodsResult, dt.byteCodeObjects);
        if (!methodsResult.status.code) {
          auto ts = getTokenStandart(methodsResult.methods);
          if (ts != TokenStandart::NotAToken) {
            Token t;
            t.standart = ts;
            t.owner = dt.deployer;

            {
              std::lock_guard<decltype(dataMut_)> lInt(dataMut_);
              tokens_[dt.address] = t;
            }
          }
        }

        l.lock();
      }

      decltype(newExecutes_) executes;
      std::swap(executes, newExecutes_);
      l.unlock();

      for (auto& st : executes) {
        {
          std::lock_guard<decltype(dataMut_)> lInt(dataMut_);
          auto tIt = tokens_.find(st.first);
          if (tIt == tokens_.end()) continue; // Ignore if not-a-token

          for (auto& ps : st.second.invocations) {
            initiateHolder(tIt->second, tIt->first, ps.initiator);
            ++tIt->second.transactionsCount;

            if (isTransfer(ps.method, ps.params)) {
              ++tIt->second.transfersCount;
              auto trPair = getTransferData(ps.initiator, ps.method, ps.params);
              if (trPair.first.is_valid())
                initiateHolder(tIt->second, tIt->first, trPair.first, true);
              if (trPair.second.is_valid())
                initiateHolder(tIt->second, tIt->first, trPair.second, true);
            }
            else if (tIt->second.standart == TokenStandart::CreditsExtended) {
              csdb::Address regDude = tryGetRegisterData(ps.method, ps.params);
              if (regDude.is_valid()) initiateHolder(tIt->second, tIt->first, regDude);
            }
          }
        }

        refreshTokenState(st.first, st.second.newState);
      }

      l.lock();
      tokCv_.wait(l);
    }
  });
}

void TokensMaster::checkNewDeploy(const csdb::Address& sc,
                                  const csdb::Address& deployer,
                                  const api::SmartContractInvocation& sci,
                                  const std::string& newState) {
  DeployTask dt;
  dt.address = sc;
  dt.deployer = deployer;
  dt.byteCodeObjects = sci.smartContractDeploy.byteCodeObjects;

  TokenInvocationData tdo;
  tdo.newState = newState;

  TokenInvocationData::Params ps;
  ps.initiator = deployer;
  tdo.invocations.push_back(ps);

  {
    std::lock_guard<decltype(cvMut_)> l(cvMut_);
    /* It is important to do this under one lock */
    deployQueue_.push(dt);
    newExecutes_[sc] = tdo;
  }

  tokCv_.notify_all();
}

void TokensMaster::checkNewState(const csdb::Address& sc,
                                 const csdb::Address& initiator,
                                 const api::SmartContractInvocation& sci,
                                 const std::string& newState) {
  TokenInvocationData::Params ps;
  ps.initiator = initiator;
  ps.method = sci.method;
  ps.params = sci.params;

  {
    std::lock_guard<decltype(cvMut_)> l(cvMut_);
    auto& tid = newExecutes_[sc];
    tid.newState = newState;
    tid.invocations.push_back(ps);
  }

  tokCv_.notify_all();
}

void TokensMaster::applyToInternal(const std::function<void(const TokensMap&,
                                                            const HoldersMap&)> func) {
  std::lock_guard<decltype(dataMut_)> l(dataMut_);
  func(tokens_, holders_);
}

bool TokensMaster::isTransfer(const std::string& method,
                              const std::vector<general::Variant>& params) {
  return isNormalTransfer(method, params) || isTransferFrom(method, params);
}

using AddrPair = std::pair<csdb::Address, csdb::Address>;

AddrPair TokensMaster::getTransferData(const csdb::Address& initiator,
                                       const std::string& method,
                                       const std::vector<general::Variant>& params) {
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

TokensMaster::TokensMaster(api::APIHandler*) { }
TokensMaster::~TokensMaster() { }
void TokensMaster::run() { }
void TokensMaster::checkNewDeploy(const csdb::Address&, const csdb::Address&, const api::SmartContractInvocation&, const std::string&) { }
void TokensMaster::checkNewState(const csdb::Address&, const csdb::Address&, const api::SmartContractInvocation&, const std::string&) { }
void TokensMaster::applyToInternal(const std::function<void(const TokensMap&, const HoldersMap&)>) { }
bool TokensMaster::isTransfer(const std::string&, const std::vector<general::Variant>&) { return false; }
std::pair<csdb::Address, csdb::Address> TokensMaster::getTransferData(const csdb::Address&, const std::string&, const std::vector<general::Variant>&) { return std::pair<csdb::Address, csdb::Address>(); }
std::string TokensMaster::getAmount(const api::SmartContractInvocation&) { return ""; }
TokenStandart TokensMaster::getTokenStandart(const std::vector<general::MethodDescription>&) { return TokenStandart::NotAToken; }

/*void TokensMaster::checkNewDeploy(const csdb::Address&, const csdb::Address&, const api::SmartContractInvocation&, const std::string&) { }
void TokensMaster::checkNewState(const csdb::Address&, const csdb::Address&, const api::SmartContractInvocation&, const std::string&) { }
void TokensMaster::applyToInternal(const std::function<void(const TokensMap&, const HoldersMap&)>) { }
bool TokensMaster::isTransfer(const std::string&, const std::vector<general::Variant>&) { return false; }
std::pair<csdb::Address, csdb::Address> TokensMaster::getTransferData(const csdb::Address&, const std::string&, const std::vector<general::Variant>&) { return std::pair<csdb::Address, csdb::Address>(); }
std::string TokensMaster::getAmount(const api::SmartContractInvocation&) { return ""; }
TokenStandart TokensMaster::getTokenStandart(const std::vector<general::MethodDescription>& methods) { return TokenStandart::NotAToken; }
*/
#endif
