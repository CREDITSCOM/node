#ifndef __TOKENS_H__
#define __TOKENS_H__
#include <condition_variable>
#include <mutex>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

#include <boost/functional/hash.hpp>

#include <csdb/address.h>

namespace std {
  template<>
  struct hash<csdb::Address> {
    size_t operator()(const csdb::Address& addr) const {
      const auto vec = addr.public_key();
      return boost::hash_range(vec.begin(), vec.end());
    }
  };
}

using TokenId = csdb::Address;
using HolderKey = csdb::Address;

enum TokenStandart {
  CreditsBasic,
  CreditsExtended
};

struct Token {
  TokenStandart standart;
  csdb::Address owner;

  std::string name;
  std::string symbol;
  std::string totalSupply;

  uint64_t transactionsCount;
  uint64_t transfersCount;

  struct HolderInfo {
    std::string balance;
    uint64_t transfersCount;
  };

  std::map<HolderKey, HolderInfo> holders;
};

using TokensMap = std::unordered_map<TokenId, Token>;
using HoldersMap = std::unordered_map<HolderKey, std::set<TokenId>>;

class TokensMaster {
public:
  ~TokensMaster();

  void run();

  void checkNewDeploy(const api::SmartContractInvocation&,
                      const std::string& newState);

  void checkNewState(const csdb::Address&,
                     const std::string& newState);

  void applyToInternal(const std::function<void(const TokensMap&,
                                                const HoldersMap&)>);

  static bool isTransfer(const api::SmartContractInvocation&);
  static std::pair<csdb::Address, csdb::Address> getTransferData(const api::SmartContractInvocation&);
  static std::string getAmount(const api::SmartContractInvocation&);

private:
  std::mutex cvMut_;
  std::condition_variable tokCv_;

  std::mutex dataMut_;
  TokensMap tokens_;
  HoldersMap holders_;

  std::atomic<bool> running_ = { false };
  std::thread tokThread_;
};

#endif // __TOKENS_H__
