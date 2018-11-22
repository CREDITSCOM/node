#include <client/params.hpp>

#include "APIHandler.h"
#include "tokens.hpp"

#ifdef TOKENS_CACHE

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
      tokCv_.wait(l);
    }
  });
}

void TokensMaster::applyToInternal(const std::function<void(const TokensMap&,
                                                            const HoldersMap&)> func) {
  std::lock_guard<decltype(dataMut_)> l(dataMut_);
  func(tokens_, holders_);
}

#else

TokensMaster::~TokensMaster() { }

void TokensMaster::run() { }
void TokensMaster::checkNewDeploy(const api::SmartContractInvocation&, const std::string&) { }
void TokensMaster::checkNewState(const csdb::Address&, const std::string&) { }

void TokensMaster::applyToInternal(const std::function<void(const TokensMap&, const HoldersMap&)>) { }

#endif
