#include <client/params.hpp>
#include <lib/system/keys.hpp>

#include <base58.h>

#include "APIHandler.h"
#include "tokens.hpp"

#ifdef TOKENS_CACHE

static inline bool isStringParam(const std::string& param) {
  return param.size() >= 2 && param[0] == '"' && param.back() == '"';
}

static inline bool isNormalTransfer(const std::string& method,
                                    const std::vector<std::string>& params) {
  return method == "transfer" && params.size() == 2 && isStringParam(params[0]) && isStringParam(params[1]);
}

static inline bool isTransferFrom(const std::string& method,
                                  const std::vector<std::string>& params) {
  return method == "transferFrom" && params.size() == 3 && isStringParam(params[0]) && isStringParam(params[1]) && isStringParam(params[1]);
}

static csdb::Address tryExtractPublicKey(const std::string& str) {
  // First try to decipher
  csdb::Address result;
  if (!isStringParam(str)) return result;

  std::string pureStr = str.substr(0, str.size() - 2);
  std::vector<uint8_t> vc;

  bool decodeSucc = DecodeBase58(pureStr, vc);
  if (!decodeSucc || vc.size() != PUBLIC_KEY_LENGTH) return result;

  return csdb::Address::from_public_key(vc);
}

static csdb::Address tryGetRegisterData(const std::string& method,
                                        const std::vector<std::string>& params) {
  if (method == "register" && params.size() == 1)
    return tryExtractPublicKey(params[0]);

  return csdb::Address();
}

static inline bool isValidAmountChar(const char c) {
  return std::isdigit(c) || c == '.' || c == ',';
}

static std::string tryExtractAmount(const std::string& str) {
  if (str.size() > 258 || !isStringParam(str)) return "0";

  const auto end = str.end() - 1;
  bool hasSep = false;

  uint8_t leadingZeros = 0; // From beginning
  uint8_t trailingZeros = 0; // After .

  bool hasNonZero = false;

  std::string result;
  for (auto cIt = str.begin() + 1; cIt != end; ++cIt) {
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

static TokenStandart
getTokenStandart(const std::vector<executor::MethodDescription>& methods) {
  const static std::map<std::string, std::pair<std::string, std::vector<std::string>>>
    StandartMethods =
    {
     { "getName", { "string", { } } },
     { "getSymbol", { "string", { } } },
     { "getDecimal", { "int", { } } },
     { "setFrozen", { "boolean", { "boolean" } } },
     { "totalSupply", { "string", { } } },
     { "balanceOf", { "string", { "string", "string" } } },
     { "allowance", { "string", { "string", "string" } } },
     { "transfer", { "boolean", { "string", "string" } } },
     { "transferFrom", { "boolean", { "string", "string", "string" } } },
     { "approve", { "void", { "string", "string" } } },
     { "burn", { "boolean", { "string" } } },
     { "register", { "void", { } } },
     { "buyTokens", { "boolean", { "string" } } }
    };

  const static auto findPos = [](const std::string& key) { return std::distance(StandartMethods.begin(), StandartMethods.find(key)); };
  const static std::pair<size_t, size_t> extendedPositions = { findPos("register"), findPos("buyTokens") };

  std::vector<bool> gotMethods(StandartMethods.size(), false);
  for (auto& m : methods) {
    auto smIt = StandartMethods.find(m.name);

    if (smIt != StandartMethods.end()) {
      if (smIt->second.second.size() != m.argTypes.size() ||
          !javaTypesEqual(smIt->second.first, m.returnType)) continue;

      bool argsMatch = true;
      for (uint32_t i = 0; i < m.argTypes.size(); ++i) {
        if (!javaTypesEqual(smIt->second.second[i], m.argTypes[i])) {
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

void TokensMaster::refreshTokenState(const csdb::Address& token,
                                     const std::string& newState) {

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

TokensMaster::TokensMaster(executor::ContractExecutorConcurrentClient* ex): executor_(ex) { }

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
        executor_->getContractMethods(methodsResult, dt.byteCode);
        if (!methodsResult.status.code) {
          auto ts = getTokenStandart(methodsResult.methods);
          if (ts != TokenStandart::NotAToken) {
            Token t;
            t.standart = ts;
            t.owner = dt.deployer;

            {
              std::lock_guard<decltype(dataMut_)> l(dataMut_);
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
          std::lock_guard<decltype(dataMut_)> l(dataMut_);
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
  dt.byteCode = sci.byteCode;

  TokenInvocationData tdo;
  tdo.newState = newState;

  TokenInvocationData::Params ps;
  ps.initiator = deployer;
  tdo.invocations.push_back(ps);

  std::lock_guard<decltype(dataMut_)> l(dataMut_);
  /* It is important to do this under one lock */
  deployQueue_.push(dt);
  newExecutes_[sc] = tdo;
}

void TokensMaster::checkNewState(const csdb::Address& sc,
                                 const csdb::Address& initiator,
                                 const api::SmartContractInvocation& sci,
                                 const std::string& newState) {
  TokenInvocationData::Params ps;
  ps.initiator = initiator;
  ps.method = sci.method;
  ps.params = sci.params;

  std::lock_guard<decltype(dataMut_)> l(dataMut_);
  auto& tid = newExecutes_[sc];
  tid.newState = newState;
  tid.invocations.push_back(ps);
}

void TokensMaster::applyToInternal(const std::function<void(const TokensMap&,
                                                            const HoldersMap&)> func) {
  std::lock_guard<decltype(dataMut_)> l(dataMut_);
  func(tokens_, holders_);
}

bool TokensMaster::isTransfer(const std::string& method,
                              const std::vector<std::string>& params) {
  return isNormalTransfer(method, params) || isTransferFrom(method, params);
}

using AddrPair = std::pair<csdb::Address, csdb::Address>;

AddrPair TokensMaster::getTransferData(const csdb::Address& initiator,
                                       const std::string& method,
                                       const std::vector<std::string>& params) {
  AddrPair result;

  if (isNormalTransfer(method, params)) {
    result.first = initiator;
    result.second = tryExtractPublicKey(params[0]);
  }
  else if (isTransferFrom(method, params)) {
    result.first = tryExtractPublicKey(params[0]);
    result.second = tryExtractPublicKey(params[1]);
  }

  return result;
}

std::string TokensMaster::getAmount(const api::SmartContractInvocation& sci) {
  if (isNormalTransfer(sci.method, sci.params))
    return tryExtractAmount(sci.params[1]);
  else if (isTransferFrom(sci.method, sci.params))
    return tryExtractAmount(sci.params[2]);

  return "0";
}

#else

TokensMaster::~TokensMaster() { }

void TokensMaster::run() { }
void TokensMaster::checkNewDeploy(const api::SmartContractInvocation&, const std::string&) { }
void TokensMaster::checkNewState(const csdb::Address&, const std::string&) { }

void TokensMaster::applyToInternal(const std::function<void(const TokensMap&, const HoldersMap&)>) { }

#endif
