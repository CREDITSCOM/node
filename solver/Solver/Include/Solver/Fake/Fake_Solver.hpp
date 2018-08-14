//
// Created by alexraag on 04.05.2018.
//

#pragma once

//#include "../Include/Solver/ISolver.hpp"
#include <vector>

#include "Solver/ISolver.hpp"

#include <csdb/csdb.h>
#include <csdb/pool.h>
#include <memory>

#include <thread>
// #include "cstimer.h"

#include <api_types.h>
#include <functional>

#include "csnode/node.hpp"

#include <client/params.hpp>

namespace Credits {

class Fake_Generals;

typedef struct Hash_
{
  Hash_::Hash_(uint8_t* a)
  {
    memcpy(val, a, 32);
  }
  Hash_::Hash_() {}
  uint8_t val[32];

};
typedef struct Signature
{
  Signature::Signature(void* a)
  {
    memcpy(val, a, 64);
  }
  Signature::Signature() {}
  uint8_t val[64];

};
typedef struct HashVector
{
  uint8_t Sender;
  uint32_t roundNum;
  Hash_ hash;
  Signature sig;
};
typedef struct HashMatrix
{
  uint8_t Sender;
  uint32_t roundNum;
  HashVector hmatr[100];
  Signature sig;
};

class Fake_Solver : public ISolver
{
  public:
    Fake_Solver(Node*);
    ~Fake_Solver();

    Fake_Solver(const Fake_Solver&) = delete;
    Fake_Solver& operator=(const Fake_Solver&) = delete;

    //signature methods
    void set_keys(const std::vector<uint8_t>& pub, const std::vector<uint8_t>& priv);


    // Solver solves stuff (even the fake one)...

    void gotTransaction(csdb::Transaction&&) override;
    void gotTransactionList(csdb::Transaction&&) override;
    void gotBlockCandidate(csdb::Pool&&) override;
    void gotVector(Vector&&, const PublicKey&) override;
    void gotMatrix(Matrix&&, const PublicKey&) override;
    void gotBlock(csdb::Pool&&, const PublicKey&) override;
    void gotHash(Hash&&, const PublicKey&) override;

    // API methods

    void addInitialBalance() override;

    void send_wallet_transaction(const csdb::Transaction& transaction);

    void nextRound() override;

#ifdef STARTER
    void startRounds();
#endif

  private:
    void _initApi();

    void runMainRound();
    void closeMainRound();

    void flushTransactions();

    void writeNewBlock();

#ifdef SPAM_MAIN
    void createPool();

    std::atomic_bool createSpam;
    std::thread spamThread;

    csdb::Pool testPool;
#endif // SPAM_MAIN

//signature verification
    bool verify_signature(uint8_t signature[64], uint8_t public_key[32], uint8_t* message, size_t message_len);
    std::vector<uint8_t> myPublicKey;
    std::vector<uint8_t> myPrivateKey;


    static void sign_transaction(const void* buffer, const size_t buffer_size);
    static void verify_transaction(const void* buffer,
                                   const size_t buffer_size);

    Node* node_;
    std::unique_ptr<Fake_Generals> generals;

    std::set<PublicKey> receivedVec_ips;
    std::set<PublicKey> receivedMat_ips;

    std::vector<Hash> hashes;
    std::vector<PublicKey> ips;

    bool receivedVecFrom[100];
    uint8_t trustedCounterVector;
    bool receivedMatFrom[100];
    uint8_t trustedCounterMatrix;

    HashVector hvector;

    std::vector<std::string> vector_datas;

    csdb::Pool m_pool;//pool to write to db
    csdb::Pool v_pool;//poot lo write transactions when the node has MAIN status

    bool m_pool_closed = true;

    bool sentTransLastRound = false;

    bool vectorComplete = false;
    bool consensusAchieved = false;
    bool blockCandidateArrived = false;

    std::mutex m_trans_mut;
    std::vector<csdb::Transaction> m_transactions;

#ifdef MAIN_RESENDER
    std::vector<csdb::Transaction> m_late_trxns;
#endif

#ifdef SPAMMER
    std::atomic_bool spamRunning{ false };
    std::thread spamThread;
    void spamWithTransactions();
#endif
};
}
