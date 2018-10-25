/**
*  @file spammer.h
*  @author Sergey Sychev
*/

#ifndef SOLVER_SPAMMER_H
#define SOLVER_SPAMMER_H

#include <cstdint>
#include <vector>
#include <thread>

#include <csdb/address.h>

class Node;
namespace csdb {
class Transaction;
} // namespace csdb;

namespace cs {

class Spammer {
 public:
  void StartSpamming(Node&);
  Spammer();
  ~Spammer();

 private:
  void GenerateTargetWallets();
  void SpamWithTransactions(Node&);
  void FundMyWallet(Node&);
  csdb::Address OptimizeAddress(const csdb::Address&, Node&);
  void SignTransaction(csdb::Transaction&, const uint8_t* private_key);

  uint8_t* public_key_;
  uint8_t* private_key_;
  std::vector<csdb::Address> target_wallets_;
  std::thread spam_thread_;
};

} // namespace cs
#endif // SOLVER_SPAMMER_H