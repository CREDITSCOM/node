////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <functional>
#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <api_types.h>
#include <csdb/csdb.h>
#include <csdb/pool.h>
#include <csdb/transaction.h>
#include <boost/asio.hpp>
//#include <csnode/node.hpp>
//#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>
#include <client/params.hpp>
#include <Solver/Fee.h>

//#define MONITOR_NODE

class Node;

namespace Credits {
typedef std::string Vector;
typedef std::string Matrix;

    class WalletsState;
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
		//uint32_t roundNum;
		Hash_ hash;
		Signature sig;
	};
	struct HashMatrix
	{
		uint8_t Sender;
		//uint32_t roundNum;
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

    class Solver {
    public:
        Solver(Node*, csdb::Address genesisAddress, csdb::Address startAddres
#ifdef SPAMMER
          , csdb::Address spammerAddress
#endif
        );
        ~Solver();

        Solver(const Solver &) = delete;
        Solver &operator=(const Solver &) = delete;

		void set_keys(const std::vector<uint8_t>& pub, const std::vector<uint8_t>& priv);

		// Solver solves stuff

		void gotTransaction(csdb::Transaction&&);
		void gotTransactionList(csdb::Pool&&);
		void gotBlockCandidate(csdb::Pool&&);
		void gotVector(HashVector&&);
		void gotMatrix(HashMatrix&&);
		void gotBlock(csdb::Pool&&, const PublicKey&);
		void gotHash(Hash&, const PublicKey&);
		void gotBlockRequest(csdb::PoolHash&&, const PublicKey&);
		void gotBlockReply(csdb::Pool&&);
    void gotBadBlockHandler(csdb::Pool&&, const PublicKey&);
    void gotIncorrectBlock(csdb::Pool&&, const PublicKey&);
    void gotFreeSyncroBlock(csdb::Pool&&);
    void sendTL();
    void rndStorageProcessing();
    void tmpStorageProcessing();
		// API methods

		void initApi();
    uint32_t getTLsize();
		void addInitialBalance();

    void send_wallet_transaction(const csdb::Transaction& transaction);


		void nextRound();
    bool mPoolClosed();
    void setLastRoundTransactionsGot(size_t trNum);
    //remove it!!!
    void buildBlock(csdb::Pool& block);

    const HashVector& getMyVector() const;
    const HashMatrix& getMyMatrix() const;
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

    void takeDecWorkaround();
		void runMainRound();
		void closeMainRound();

		void flushTransactions();

		void writeNewBlock();
    void prepareBlockForSend(csdb::Pool& block);
    csdb::Pool removeTransactionsWithBadSignatures(const csdb::Pool& pool);
		
		std::vector<uint8_t> myPublicKey;
		std::vector<uint8_t> myPrivateKey;

		Node* node_;
        std::unique_ptr<WalletsState> walletsState;
        std::unique_ptr<Generals> generals;

        const csdb::Address genesisAddress;
        const csdb::Address startAddress;
#ifdef SPAMMER
        const csdb::Address spammerAddress;
#endif

        HashVector hvector;
		
		
    size_t lastRoundTransactionsGot;
		std::set<PublicKey> receivedVec_ips;
		bool receivedVecFrom[100];
		uint8_t trustedCounterVector;

		std::set<PublicKey> receivedMat_ips;
		bool receivedMatFrom[100];
		uint8_t trustedCounterMatrix;
    void checkMatrixCame();


		std::vector<Hash> hashes;
		std::vector<PublicKey> ips;

		std::vector<std::string> vector_datas;

    csdb::Pool m_pool;
		
		//std::vector<csdb::Transaction> v_pool;

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
    std::mutex m_trans_mut;
    csdb::Pool m_transactions;
    Fee fee_counter_;
    /*to store new blocks*/
    std::map <size_t, csdb::Pool> tmpStorage;
    /*to store unrequested syncro blocks*/
    std::map <size_t, csdb::Pool> rndStorage;

#ifdef SPAMMER
		std::atomic_bool spamRunning{ false };
		std::thread spamThread;
		void spamWithTransactions();
    std::vector<csdb::Address> spam_keys;
#endif

	};
}
