/**
 *  @file spammer.h
 *  @author Sergey Sychev
 */

#ifndef SOLVER_SPAMMER_HPP
#define SOLVER_SPAMMER_HPP

#include <cstdint>
#include <thread>
#include <vector>

#include <cscrypto/cscrypto.hpp>
#include <csdb/address.hpp>

class Node;
namespace csdb {
class Transaction;
}  // namespace csdb

namespace cs {
/**
 *  @brief This spammer creates transactions and
 *  sends them in a separate thread.
 *
 *  Firstly, it generates own private and public keys.
 *  Then it funds own public keys with money from test address in genesis block
 *  and starts spam with transactions among own keys.
 *
 *  Signature verification for spammer transactions may not be switched off,
 *  because spammer signs each transaction. Also it fills source and target
 *  in transactions in accordance with CS wallet cache system.
 */
class Spammer {
public:
    void StartSpamming(Node&);
    Spammer() = default;
    ~Spammer() = default;

    Spammer(const Spammer&) = delete;
    Spammer(Spammer&&) = delete;
    const Spammer& operator=(const Spammer&) = delete;

private:
    void GenerateMyWallets();
    void SpamWithTransactions(Node&);
    void FundMyWallets(Node&);
    csdb::Address OptimizeAddress(const csdb::Address&, Node&);
    void SignTransaction(csdb::Transaction&, const std::vector<cscrypto::Byte>& private_key);
    void SignTransaction(csdb::Transaction&, const cscrypto::PrivateKey& private_key);

    // wallets to which spammer sends transactions
    std::vector<std::pair<csdb::Address, cscrypto::PrivateKey>> my_wallets_;
    // thread for: void SpamWithTransactions(Node&)
    std::thread spam_thread_;
};

}  // namespace cs
#endif  // SOLVER_SPAMMER_HPP
