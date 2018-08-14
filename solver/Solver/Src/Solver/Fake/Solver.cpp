//
// Created by alexraag on 04.05.2018.
//

#include <iostream>
#include <random>
#include <sstream>

#include <sys/timeb.h>

#include <csdb/address.h>
#include <csdb/currency.h>
#include <csdb/wallet.h>

#include <csnode/node.hpp>

#include "Solver/Fake/Generals.hpp"
#include "Solver/Fake/Solver.hpp"
#include <algorithm>

#include <lib/system/logger.hpp>

#include <base58.h>

#include <sodium.h>



namespace Credits {

using ScopedLock = std::lock_guard<std::mutex>;
constexpr short min_nodes = 3;

Solver::Solver(Node* node)
  : node_(node)
  , generals(std::unique_ptr<Generals>(new Generals()))
  , vector_datas()
  , m_pool()
{}

Solver::~Solver()
{
  //		csconnector::stop();
  //		csstats::stop();
}

void Solver::set_keys(const std::vector<uint8_t>& pub, const std::vector<uint8_t>& priv)
{
	myPublicKey = pub;
	myPrivateKey = priv;
}

static void addTimestampToPool(csdb::Pool& pool)
{

}

void Solver::prepareBlockForSend(csdb::Pool& block)
{
  struct timeb t;
  ftime(&t);
  //addTimestampToPool(block);
  block.set_writer_public_key(myPublicKey);
  block.add_user_field(0, std::to_string((uint64_t)((uint64_t)(t.time) * 1000ll) + t.millitm));
  block.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);
  block.sign_pool(myPrivateKey);
  std::cout << "last sequence: " << (node_->getBlockChain().getLastWrittenSequence()) << ", last time:" << node_->getBlockChain().loadBlock(node_->getBlockChain().getLastHash()).user_field(0).value<std::string>().c_str() << std::endl;
  std::cout << "prev_hash: " << node_->getBlockChain().getLastHash().to_string() << " <- Not sending!!!" << std::endl;
  std::cout << "new sequence: " << block.sequence() << ", new time:" << block.user_field(0).value<std::string>().c_str() << std::endl;
}

void Solver::closeMainRound()
{
  if (v_pool.transactions_count() > 0)
  {
	 m_pool_closed = true; 
	 
	  for (auto& it : node_->getConfidants())
	  { 
		  std::cout << "Solver -> Sending TransactionList to " << it.to_string() << std::endl;
		  node_->sendTransactionList(std::move(v_pool), it); // Correct sending, better if to all one time
	  }
  }
  else
  {
    node_->becomeWriter();
	std::cout << "Solver -> Node Level changed 2 -> 3" << std::endl;
    //addTimestampToPool(m_pool);

#ifdef SPAM_MAIN
    createSpam = false;
    spamThread.join();
    prepareBlockForSend(testPool);
    node_->sendBlock(testPool);
#else
    prepareBlockForSend(m_pool);
	std::cout << "Solver -> new sequence: " << m_pool.sequence() << ", new time:" << m_pool.user_field(0).value<std::string>().c_str() << std::endl;
    node_->sendBlock(std::move(m_pool));
	std::cout << "Solver -> Block is sent ... awaiting hashes" << std::endl;
#endif
	node_->getBlockChain().setGlobalSequence(m_pool.sequence());
	std::cout << "Solver -> Global Sequence: "  << node_->getBlockChain().getGlobalSequence() << std::endl;
	std::cout << "Solver -> Writing New Block"<< std::endl;
    node_->getBlockChain().putBlock(m_pool);
	
  }
}

void Solver::runMainRound()
{
  m_pool_closed = false;
  node_->runAfter(std::chrono::milliseconds(5000),
                  [this]() { closeMainRound(); });
}

void Solver::flushTransactions()
{
	if (node_->getMyLevel() != NodeLevel::Normal) { return; }
	{
    std::lock_guard<std::mutex> l(m_trans_mut);
    if (m_transactions.size()) {
      node_->sendTransaction(std::move(m_transactions));
      sentTransLastRound = true;
	  //std::cout << "FlushTransaction ..." << std::endl;
      m_transactions.clear();
    } else {
      return;
    }
  }
  node_->runAfter(std::chrono::milliseconds(50),
                  [this]() { flushTransactions(); });
}

void Solver::gotTransaction(csdb::Transaction&& transaction)
{
	
	if (m_pool_closed) return;
	// LOG_EVENT("m_pool_closed already, cannot accept your transactions");
	//std::cout << "SOLVER> Got Transaction" << std::endl;

	if (transaction.is_valid())
		{
			auto v = transaction.to_byte_stream_for_sig();
			size_t msg_len = v.size();
			uint8_t* message = new uint8_t[msg_len];
			for (int i = 0; i < msg_len; i++)
				message[i] = v[i];

			auto vec = transaction.source().public_key();
			uint8_t public_key[32];
			for (int i = 0; i < 32; i++)
				public_key[i] = vec[i];

			std::string sig_str = transaction.signature();
			uint8_t* signature;
			signature = (uint8_t*)sig_str.c_str();

			//if (verify_signature(signature, public_key, message, msg_len))
			//{

			//if (v_pool.size() == 0)
			//node_->sendFirstTransaction(transaction);

					v_pool.add_transaction(transaction);
			//}
			/*else
			{
				LOG_EVENT("Wrong signature");
			}*/

			delete[]message;
		}
		else
		{
		// LOG_EVENT("Invalid transaction received");
		}
}

void Solver::gotTransactionList(csdb::Pool&& _pool)
{
	std::cout << "SOLVER> GotTransactionList" << std::endl;
	memset(receivedVecFrom, 0, 100);
	memset(receivedMatFrom, 0, 100);
	trustedCounterVector = 0;

	trustedCounterMatrix = 0;
	Hash_ result = generals->buildvector(_pool);

	receivedVecFrom[node_->getMyConfNumber()] = true;
	hvector.Sender = node_->getMyConfNumber();
	hvector.hash = result;
	receivedVecFrom[node_->getMyConfNumber()] = true;
	generals->addvector(hvector);
	node_->sendVector(std::move(hvector));
	trustedCounterVector++;
}

void Solver::gotVector(HashVector&& vector)
{
	//std::cout << "SOLVER> GotVector" << std::endl;
  uint8_t numGen = node_->getConfidants().size();
  if (vector.roundNum==node_->getRoundNumber())
  {
	  std::cout << "SOLVER> This is not the information of this round" << std::endl;
	  return;
  }
  if (receivedVecFrom[vector.Sender]==true) 
  {
		std::cout << "SOLVER> I've already got the vector from this Node" << std::endl;
		return;
  }
  receivedVecFrom[vector.Sender] = true;
  generals->addvector(vector);//building matrix
  trustedCounterVector++;

  if (trustedCounterVector == numGen)
  {
	  //std::cout << "SOLVER> GotVector : " << std::endl;
    vectorComplete = true;

	memset(receivedVecFrom, 0, 100);
	trustedCounterVector = 0;
	//compose and send matrix!!!
    //receivedMat_ips.insert(node_->getMyId());
	generals->addSenderToMatrix(node_->getMyConfNumber());
	receivedMatFrom[node_->getMyConfNumber()] = true;
	trustedCounterMatrix++;
	node_->sendMatrix(generals->getMatrix());
	generals->addmatrix(generals->getMatrix(), node_->getConfidants());//MATRIX SHOULD BE DECOMPOSED HERE!!!
  }
}

void Solver::gotMatrix(HashMatrix&& matrix)
{
	//std::cout << "SOLVER> Got Matrix" << std::endl;
	uint8_t numGen = node_->getConfidants().size();
	if (receivedMatFrom[matrix.Sender] == true)
	{
		std::cout << "SOLVER> I've already got the matrix from this Node" << std::endl;
		return;
	}
	receivedMatFrom[matrix.Sender] = true;
	trustedCounterMatrix++;
	generals->addmatrix(matrix, node_->getConfidants());//MATRIX SHOULD BE DECOMPOSED HERE!!!
	
  if (trustedCounterMatrix == numGen)
  {
	  memset(receivedMatFrom, 0, 100);
	  trustedCounterMatrix = 0;
	  uint8_t wTrusted = (generals->take_decision(node_->getConfidants(), node_->getMyConfNumber(),node_->getBlockChain().getLastHash()));
 
	  if (wTrusted == 100)
	  {
		  std::cout << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!" << std::endl;
		  writeNewBlock();
	  }

	  else
	  {
		  consensusAchieved = true;
		  if (wTrusted == node_->getMyConfNumber())
		  {
			  node_->becomeWriter();
			  composeBlock();
			  writeNewBlock();
		  }

	  }
	}
}


void Solver::composeBlock()
{

}

//what block does this function write???
void Solver::writeNewBlock()
{
	std::cout << "Solver -> writeNewBlock ... start";
  if (consensusAchieved &&
      node_->getMyLevel() == NodeLevel::Writer) {
    //m_pool.set_writer_public_key(myPublicKey);
	prepareBlockForSend(m_pool);
	//m_pool.set_sequence(node_->getBlockChain().getLastWrittenSequence()+1);
    //m_pool.sign_pool(myPrivateKey);
    node_->sendBlock(std::move(m_pool));
    node_->getBlockChain().putBlock(m_pool);
    
	std::cout << "Solver -> writeNewBlock ... finish" << std::endl;
	consensusAchieved = false;
  }
}

void Solver::gotBlock(csdb::Pool&& block, const PublicKey& sender)
{
#ifdef MONITOR_NODE
  addTimestampToPool(block);
#endif
  uint32_t g_seq = block.sequence();
  std::cout << "GOT NEW BLOCK: global sequence = " << g_seq << std::endl;
  node_->getBlockChain().setGlobalSequence(g_seq);
  if (g_seq == node_->getBlockChain().getLastWrittenSequence() + 1)
  {
		//std::cout << "Solver -> getblock calls writeLastBlock" << std::endl;
		if(block.verify_pool_signature())
			node_->getBlockChain().putBlock(block);
		if ((node_->getMyLevel() != NodeLevel::Writer) || (node_->getMyLevel() != NodeLevel::Main))
		{
			//std::cout << "Solver -> before sending hash to writer" << std::endl;
			csdb::PoolHash test_hash = node_->getBlockChain().getLastHash();//SENDING HASH!!!
			node_->sendHash(test_hash, sender);
			
		}
		//std::cout << "Solver -> finishing gotBlock" << std::endl;
  }
#ifndef SPAMMER
#ifndef MONITOR_NODE
  //if (!sentTransLastRound) {
  //  Hash test_hash = "zpa02824qsltp";
  //  node_->sendHash(test_hash, sender);
  //}
#endif
#endif
}

void Solver::gotBlockCandidate(csdb::Pool&& block)
{
	std::cout << "Solver -> getBlockCanditate" << std::endl;
  if (blockCandidateArrived)
    return;
  
  //m_pool = std::move(block);

  blockCandidateArrived = true;
 // writeNewBlock();
}

void Solver::gotHash(csdb::PoolHash&& hash, const PublicKey& sender)
{
	if (round_table_sent) return;
	//std::cout << "Solver -> gotHash: " << hash.to_string() << "from sender: " << sender.to_string() << std::endl;//<-debug feature
	csdb::PoolHash myHash(node_->getBlockChain().getLastHash());
	std::cout << "Solver -> My Hash: " << myHash.to_string() << std::endl;
 
	if (ips.size() <= min_nodes) 
	{
		if (hash == myHash) 
		{
			std::cout << "Solver -> Hashes are good" << std::endl;
			//hashes.push_back(hash);
			ips.push_back(sender);
		} 
		else
		{
			if (hash != myHash) std::cout << "Hashes do not match!!!" << std::endl;
			return;
		}
	}
	else
	{
		std::cout << "Solver -> We have enough hashes!" << std::endl;
		return;
	}

	
	if ((ips.size() == min_nodes + 1) && (!round_table_sent)) 
	{
		//node_->initNextRound(node_->getMyId(), std::move(ips));
		std::cout << "Solver -> sending NEW ROUND table" << std::endl;
		node_->sendRoundTable(node_->getMyPublicKey(), std::move(ips));
		round_table_sent = true;
		
	}
  }

void Solver::initApi()
{
  _initApi();
}

void Solver::_initApi()
{
  //        csconnector::start(&(node_->getBlockChain()),csconnector::Config{});
  //
  //		csstats::start(&(node_->getBlockChain()));
}

  /////////////////////////////

#ifdef SPAM_MAIN
static int
randFT(int min, int max)
{
  return rand() % (max - min + 1) + min;
}

void
Solver::createPool()
{
  std::string mp = "0123456789abcdef";
  const unsigned int cmd = 6;

  struct timeb tt;
  ftime(&tt);
  srand(tt.time * 1000 + tt.millitm);

  testPool = csdb::Pool();

  std::string aStr(64, '0');
  std::string bStr(64, '0');

  uint32_t limit = randFT(5, 15);

  if (randFT(0, 150) == 42) {
    csdb::Transaction smart_trans;
    smart_trans.set_currency(csdb::Currency("CS"));

    smart_trans.set_target(Credits::BlockChain::getAddressFromKey(
      "3SHCtvpLkBWytVSqkuhnNk9z1LyjQJaRTBiTFZFwKkXb"));
    smart_trans.set_source(csdb::Address::from_string(
      "0000000000000000000000000000000000000000000000000000000000000001"));

    smart_trans.set_amount(csdb::Amount(1, 0));
    smart_trans.set_balance(csdb::Amount(100, 0));

    api::SmartContract sm;
    sm.address = "3SHCtvpLkBWytVSqkuhnNk9z1LyjQJaRTBiTFZFwKkXb";
    sm.method = "store_sum";
    sm.params = { "123", "456" };

    smart_trans.add_user_field(0, serialize(sm));

    testPool.add_transaction(smart_trans);
  }

  csdb::Transaction transaction;
  transaction.set_currency(csdb::Currency("CS"));

  while (createSpam && limit > 0) {
    for (size_t i = 0; i < 64; ++i) {
      aStr[i] = mp[randFT(0, 15)];
      bStr[i] = mp[randFT(0, 15)];
    }

    transaction.set_target(csdb::Address::from_string(aStr));
    transaction.set_source(csdb::Address::from_string(bStr));

    transaction.set_amount(csdb::Amount(randFT(1, 1000), 0));
    transaction.set_balance(
      csdb::Amount(transaction.balance().integral() + 1, 0));

    testPool.add_transaction(transaction);
    --limit;
  }

  addTimestampToPool(testPool);
}
#endif

#ifdef SPAMMER

static inline int
randFT(int min, int max)
{
  return rand() % (max - min) + min;
}

void
Solver::spamWithTransactions()
{
	if (node_->getMyLevel() != Normal) return;

  std::string mp = "1234567890abcdef";

  // std::string cachedBlock;
  // cachedBlock.reserve(64000);

  std::this_thread::sleep_for(std::chrono::seconds(5));

  auto aaa = csdb::Address::from_string(
    "0000000000000000000000000000000000000000000000000000000000000001");
  auto bbb = csdb::Address::from_string(
    "0000000000000000000000000000000000000000000000000000000000000002");

  csdb::Transaction transaction;
  transaction.set_target(aaa);
  transaction.set_source(bbb);

  transaction.set_currency(csdb::Currency("CS"));

  while (true) {
    if (spamRunning) {
      {
        transaction.set_amount(csdb::Amount(randFT(1, 1000), 0));
        transaction.set_balance(
          csdb::Amount(transaction.amount().integral() + 1, 0));

        std::lock_guard<std::mutex> l(m_trans_mut);
        m_transactions.push_back(transaction);
      }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
}
#endif

///////////////////

void Solver::send_wallet_transaction(const csdb::Transaction& transaction)
{
  SUPER_TIC();
  std::lock_guard<std::mutex> l(m_trans_mut);
  SUPER_TIC();
  m_transactions.push_back(transaction);
}

void Solver::addInitialBalance()
{
  std::cout << "===SETTING DB===" << std::endl;
  const std::string start_address =
    "0000000000000000000000000000000000000000000000000000000000000002";

  csdb::Pool pool;
  csdb::Transaction transaction;
  transaction.set_target(
    csdb::Address::from_public_key(node_->getMyPublicKey().str));
  transaction.set_source(csdb::Address::from_string(start_address));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(10000, 0));
  transaction.set_balance(csdb::Amount(100, 0));

  {
	  std::lock_guard<std::mutex> l(m_trans_mut);
	  m_transactions.push_back(transaction);
  }

#ifdef SPAMMER
  spamThread = std::thread(&Solver::spamWithTransactions, this);
  spamThread.detach();
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////// gotBlockRequest
void Solver::gotBlockRequest(csdb::PoolHash&& hash, const PublicKey& nodeId) {
	csdb::Pool pool = node_->getBlockChain().loadBlock(hash);
	if (pool.is_valid())
	{
		csdb::PoolHash prev_hash;
		prev_hash.from_string("");
		pool.set_previous_hash(prev_hash);
		node_->sendBlockReply(std::move(pool), nodeId);
	}

}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////// gotBlockReply
void Solver::gotBlockReply(csdb::Pool&& pool) {
	std::cout << "Solver -> Got Block for my Request: " << pool.sequence() << std::endl;
	if (pool.sequence() == node_->getBlockChain().getLastWrittenSequence() + 1)
		node_->getBlockChain().putBlock(pool);
	

}

void Solver::nextRound()
{
	std::cout << "Solver -> Starting ... nextRound" << std::endl;
  receivedVec_ips.clear();
  receivedMat_ips.clear();

  hashes.clear();
  ips.clear();
  vector_datas.clear();

  vectorComplete = false;
  consensusAchieved = false;
  blockCandidateArrived = false;

  round_table_sent = false;
  sentTransLastRound = false;

  m_pool = csdb::Pool{};
  v_pool = csdb::Pool{};
  if (node_->getMyLevel() == NodeLevel::Main) {
    runMainRound();
#ifdef SPAM_MAIN
    createSpam = true;
    spamThread = std::thread(&Solver::createPool, this);
#endif
#ifdef SPAMMER
    spamRunning = false;
#endif
  } else {
#ifdef SPAMMER
    spamRunning = true;
#endif
    m_pool_closed = true;
    flushTransactions();
  }
}

bool Solver::verify_signature(uint8_t signature[64], uint8_t public_key[32],
									uint8_t* message, size_t message_len)
{
	int ver_ok = crypto_sign_ed25519_verify_detached(signature, message, message_len, public_key);
	if (ver_ok == 0)
		return true;
	else
		return false;
}

}
