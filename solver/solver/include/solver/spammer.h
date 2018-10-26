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
/**
*  @brief This spammer creates transactions and
*  sends them in a separate thread.
*  
*  Firstly, it generates own private and public
*  key, and target public keys. Then it funds own
*  public key with money from test address in genesis block
*  and starts spam with transactions from own public key to
*  target keys.
*
*  Signature verification for spammer transactions may not be switched off,
*  because spammer signs each transaction. Also it fills source and target
*  in transactions in accordance with CS wallet cache system.
*/
class Spammer {
 public:
  void StartSpamming(Node&);
  Spammer();
  ~Spammer();

  Spammer(const Spammer&) = delete;
  Spammer(Spammer&&) = delete;
  const Spammer& operator=(const Spammer&) = delete;

 private:
  void GenerateTargetWallets();
  void SpamWithTransactions(Node&);
  void FundMyWallet(Node&);
  csdb::Address OptimizeAddress(const csdb::Address&, Node&);
  void SignTransaction(csdb::Transaction&, const uint8_t* private_key);

  // own public key from which spammer sends transactions
  uint8_t* public_key_;
  // own private key, which is used to sign transactions
  uint8_t* private_key_;
  // wallets to which spammer sends transactions
  std::vector<csdb::Address> target_wallets_;
  // thread for: void SpamWithTransactions(Node&)
  std::thread spam_thread_;
};

} // namespace cs
#endif // SOLVER_SPAMMER_H