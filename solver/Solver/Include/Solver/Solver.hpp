////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <csdb/csdb.h>
#include <memory>
#include <thread>

#include <api_types.h>
#include <functional>

#include <atomic>
#include <functional>
#include <shared_mutex>

#include <set>
#include <string>
#include <thread>
#include <vector>

#include <api_types.h>
#include <csdb/transaction.h>
#include <boost/asio.hpp>

#include <lib/system/keys.hpp>
#include <client/params.hpp>
#include <Timer/Timer.h>
#include "../../../../csnode/include/csnode/nodecore.h"

//#define MONITOR_NODE
//#define SPAM_MAIN

class Node;

namespace Credits {
    typedef std::string Vector;
    typedef std::string Matrix;

    class Generals;
    struct Hash_
    {
        Hash_(uint8_t* a)
        {
            memcpy(val, a, 32);
        }
        Hash_() {}
        uint8_t val[32];

    };
    struct Signature
    {
        Signature(void* a)
        {
            memcpy(val, a, 64);
        }
        Signature() {}
        uint8_t val[64];

    };
#pragma pack(push, 1)
    struct HashVector
    {
        uint8_t Sender;
        Hash_ hash;
        Signature sig;
    };
    struct HashMatrix
    {
        uint8_t Sender;
        HashVector hmatr[100];
        Signature sig;
    };
    struct NormalState
    {
        bool isOn;
        bool rtStartReceived;
        bool transactionSend;
        bool newBlockReceived;
        bool hashSent;
    };
    struct MainState
    {
        bool isOn;
        bool rtStartReceived;
        bool transactinReceived;
        bool newBlockReceived;
        bool rtFinishReceived;
        bool tlSent;
    };
    struct TrustedState
    {
        bool isOn;
        bool rtStartReceived;
        bool tlReceived;
        bool vectorSent;
        bool allVectorsReceived;
        bool matrixSent;
        bool allMatricesReceived;
        bool writerConfirmationSent;
        bool newBlockReceived;
        bool hashSent;
    };
    struct WriterState
    {
        bool isOn;
        bool writerConfirmationReceived;
        bool newBlockSent;
        bool hashesReceived;
        bool trSent;
    };

    struct SolverStates
    {
        NormalState normal;
        MainState main;
        TrustedState trusted;
        WriterState writer;

    };

#pragma pack(pop)
    class State {
    };

    class Solver
    {
    public:
        Solver(Node*);
        ~Solver();

        Solver(const Solver &) = delete;
        Solver &operator=(const Solver &) = delete;

        void set_keys(const std::vector<uint8_t>& pub, const std::vector<uint8_t>& priv);

        // Solver solves stuff
        void gotTransaction(csdb::Transaction&&);
        void gotTransactionsPacket(cs::TransactionsPacket&& packet);
        void gotPacketHashesRequest(std::vector<cs::TransactionsPacketHash>&& hashes);
        void gotPacketHashesReply(cs::TransactionsPacket&& packet);
        void gotRound(cs::RoundInfo&& round);
        void gotTransactionList(csdb::Pool&&);
        void gotBlockCandidate(csdb::Pool&&);
        void gotVector(HashVector&&);
        void gotMatrix(HashMatrix&&);
        void gotBlock(csdb::Pool&&, const PublicKey&);
        void gotHash(Hash&, const PublicKey&);
        void gotBlockRequest(csdb::PoolHash&&, const PublicKey&);
        void gotBlockReply(csdb::Pool&&);
        void gotBadBlockHandler(csdb::Pool&&, const PublicKey&);
        void sendTL();
        void applyCharacteristic(const std::vector<uint8_t>& characteristic, const uint32_t bitsCount,
                                 const csdb::Pool& metaInfoPool);

        // API methods
        void initApi();
        uint32_t getTLsize();
        void addInitialBalance();

        cs::RoundNumber currentRoundNumber();
        void addTransaction(const csdb::Transaction& transaction);

        void send_wallet_transaction(const csdb::Transaction& transaction);

        void nextRound();
        bool mPoolClosed();
        void setLastRoundTransactionsGot(size_t trNum);

        //remove it!!!
        void buildBlock(csdb::Pool& block);

        HashVector getMyVector();
        HashMatrix getMyMatrix();
        void initConfRound();
        void sendZeroVector();
        void checkVectorsReceived(size_t _rNum);
        void checkMatrixReceived();
        void addConfirmation(uint8_t confNumber_);
        bool getIPoolClosed();
        bool getBigBangStatus();
        void setBigBangStatus(bool _status);
        void setRNum(size_t _rNum);

    private:
        void _initApi();

        void runMainRound();
        void closeMainRound();

        void flushTransactions();

        void writeNewBlock();
        void prepareBlockForSend(csdb::Pool& block);

#ifdef SPAM_MAIN
        void createPool();

        std::atomic_bool createSpam;
        std::thread spamThread;

        csdb::Pool testPool;
#endif //SPAM_MAIN

        bool verify_signature(uint8_t signature[64], uint8_t public_key[32], uint8_t* message, size_t message_len);

        std::vector<uint8_t> myPublicKey;
        std::vector<uint8_t> myPrivateKey;

        Node* node_;
        std::unique_ptr<Generals> generals;

        HashVector hvector;


        size_t lastRoundTransactionsGot;
        std::set<PublicKey> receivedVec_ips;
        bool receivedVecFrom[100];
        uint8_t trustedCounterVector;

        std::set<PublicKey> receivedMat_ips;
        bool receivedMatFrom[100];
        uint8_t trustedCounterMatrix;

        std::vector<Hash> hashes;
        std::vector<PublicKey> ips;

        std::vector<std::string> vector_datas;

        csdb::Pool m_pool;
        csdb::Pool m_uncharacterizedPool;

        cs::RoundInfo mRound;

        csdb::Pool v_pool;
        csdb::Pool b_pool;
        bool m_pool_closed = true;
        bool sentTransLastRound = false;
        bool vectorComplete = false;
        bool consensusAchieved = false;
        bool blockCandidateArrived = false;
        bool round_table_sent = false;
        bool transactionListReceived = false;
        bool vectorReceived = false;
        bool gotBlockThisRound = false;
        bool writingConfirmationGot = false;
        bool gotBigBang = false;

        bool writingConfGotFrom[100];
        uint8_t writingCongGotCurrent;
        size_t rNum = 0;

        cs::SharedMutex mHashTableMutex;
        cs::SpinLock mSpinLock;

        std::vector<csdb::Transaction> m_transactions;
        csdb::Pool m_transactions_;

        cs::TransactionsPacketHashTable mHashTable;
        cs::TransactionsBlock mTransactionsBlock;

        Credits::CTimer m_SendingPacketTimer;

#ifdef SPAMMER
        std::atomic_bool spamRunning{ false };
        std::thread spamThread;
        void spamWithTransactions();
#endif
    };
}
