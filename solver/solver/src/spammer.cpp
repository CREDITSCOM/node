/**
*  @file spammer.cpp
*  @author Sergey Sychev
*/

#include "solver/spammer.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <cstdlib>

#include <base58.h>
#include <sodium.h>
#include <csdb/amount.h>
#include <csdb/amount_commission.h>
#include <csdb/currency.h>
#include <csdb/internal/types.h>
#include <csdb/transaction.h>
#include <csnode/node.hpp>

namespace cs {
namespace {
constexpr auto kPublicKeySize = 32;
constexpr auto kPrivateKeySize = 64;
constexpr auto kSignatureLength = 64;
// number of wallets, which will be generated to send transactions to
constexpr auto kTargetWalletsNum = 10;
constexpr auto kTimeStartSleepSec = 5;
constexpr auto kSpammerSleepTimeMicrosec = 70000;
// from this address public_key_ will be fund, genesis block address for test purposes
std::string kGenesisPublic = "5B3YXqDTcWQFGAqEJQJP3Bg1ZK8FFtHtgCiFLT5VAxpe";
std::string kGenesisPrivate = "3rUevsW5xfob6qDxWMDFwwTQCq39SYhzstuyfUGSDvF2QHBRyPD8fSk49wFXaPk3GztfxtuU85QHfMV3ozfqa7rN";
} // namespace

Spammer::Spammer() {
  public_key_ = new uint8_t[kPublicKeySize];
  private_key_ = new uint8_t[kPrivateKeySize];
  crypto_sign_keypair(public_key_, private_key_);
}

Spammer::~Spammer() {
  delete[]public_key_;
  delete[]private_key_;
}

void Spammer::StartSpamming(Node& node) {
  GenerateTargetWallets();
  spam_thread_ = std::thread(&Spammer::SpamWithTransactions, this, std::ref(node));
  spam_thread_.detach();
}

void Spammer::GenerateTargetWallets() {
  uint8_t public_key[kPublicKeySize];
  uint8_t private_key[kPrivateKeySize];
  for (int i = 0; i < kTargetWalletsNum; ++i) {
    crypto_sign_keypair(public_key, private_key);
    target_wallets_.push_back(csdb::Address::from_public_key(reinterpret_cast<const char*>(public_key)));
  }
}

void Spammer::SpamWithTransactions(Node& node) {
  std::this_thread::sleep_for(std::chrono::seconds(kTimeStartSleepSec));
  FundMyWallet(node);
  csdb::Transaction transaction;
  transaction.set_currency(csdb::Currency(1));
  transaction.set_amount(csdb::Amount(1, 0));
  transaction.set_max_fee(csdb::AmountCommission(0.1));
  transaction.set_counted_fee(csdb::AmountCommission(0.0));
  size_t target_wallet_counter = 0;
  int64_t inner_id_counter = 1;
  while (true) {
      transaction.set_source(OptimizeAddress(csdb::Address::from_public_key(
        reinterpret_cast<const char*>(public_key_)), node));
      transaction.set_target(OptimizeAddress(target_wallets_[target_wallet_counter], node));
      transaction.set_innerID(inner_id_counter);
      SignTransaction(transaction, private_key_);
      node.getSolver()->send_wallet_transaction(transaction);
      ++inner_id_counter;
      ++target_wallet_counter;

      if (target_wallet_counter == kTargetWalletsNum) {
        target_wallet_counter = 0;
      }

      std::this_thread::sleep_for(std::chrono::microseconds(kSpammerSleepTimeMicrosec));
  }
}

void Spammer::FundMyWallet(Node& node) {
  csdb::Transaction transaction;
  csdb::Address genesis_address;
  std::vector<uint8_t> genesis;
  DecodeBase58(kGenesisPublic, genesis);
  genesis_address = csdb::Address::from_public_key(genesis);
  transaction.set_source(OptimizeAddress(genesis_address, node));
  transaction.set_target(csdb::Address::from_public_key(reinterpret_cast<const char*>(public_key_)));
  transaction.set_currency(csdb::Currency(1));
  transaction.set_amount(csdb::Amount(10'000'000, 0));
  transaction.set_max_fee(csdb::AmountCommission(0.1));
  transaction.set_counted_fee(csdb::AmountCommission(0.0));
  srand(time(0));
  transaction.set_innerID((rand() + 2) & 0x3fffffffffff);
  genesis.clear();
  DecodeBase58(kGenesisPrivate, genesis);
  SignTransaction(transaction, genesis.data());
  node.getSolver()->send_wallet_transaction(transaction);
}

csdb::Address Spammer::OptimizeAddress(const csdb::Address& address, Node& node) {
  csdb::internal::WalletId id;
  if (node.getBlockChain().findWalletId(address, id)) {
    return csdb::Address::from_wallet_id(id);
  }
  return address;
}

void Spammer::SignTransaction(csdb::Transaction& transaction, const uint8_t* private_key) {
  auto transaction_bytes = transaction.to_byte_stream_for_sig();
  unsigned long long signature_length = 0;
  uint8_t signature[kSignatureLength];
  crypto_sign_ed25519_detached(signature, &signature_length,
              transaction_bytes.data(), transaction_bytes.size(), private_key);
  transaction.set_signature(std::string(signature, signature + signature_length));
}
 
} // namespace cs
