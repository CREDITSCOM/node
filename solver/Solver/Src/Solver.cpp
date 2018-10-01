////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <iostream>
#include <random>
#include <sstream>
#include <fstream>
#include <chrono>

#include <csdb/address.h>
#include <csdb/currency.h>
#include <csdb/wallet.h>
#include <csdb/amount_commission.h>

#include <csnode/node.hpp>

#include "Solver/Generals.hpp"
#include "Solver/Solver.hpp"
#include "Solver/WalletsState.h"
#include <algorithm>
#include <cmath>

#include <lib/system/logger.hpp>

#ifdef SPAMMER
  static const int NUM_OF_SPAM_KEYS = 10;
#endif

namespace {
void addTimestampToPool(csdb::Pool& pool)
{
  auto now_time = std::chrono::system_clock::now();
  pool.add_user_field(0, std::to_string(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      now_time.time_since_epoch()).count()));
}

void runAfter(const std::chrono::milliseconds& ms, std::function<void()> cb)
{    
 // std::cout << "SOLVER> Before calback" << std::endl;
  const auto tp = std::chrono::system_clock::now() + ms;
  std::thread tr([tp, cb]() {

    std::this_thread::sleep_until(tp);
  //  LOG_WARN("Inserting callback");
    CallsQueue::instance().insert(cb);
  });
 // std::cout << "SOLVER> After calback" << std::endl;
  tr.detach();
}

#if defined(SPAMMER)
static int
randFT(int min, int max)
{
  return rand() % (max - min + 1) + min;
}
#endif
} // anonimous namespace

namespace Credits {
using ScopedLock = std::lock_guard<std::mutex>;
constexpr short min_nodes = 3;

Solver::Solver(Node* node, csdb::Address _genesisAddress, csdb::Address _startAddress
#ifdef SPAMMER
  , csdb::Address _spammerAddress
#endif
  )
  : node_(node)
  , walletsState(new WalletsState(node->getBlockChain()))
  , generals(std::unique_ptr<Generals>(new Generals(*walletsState)))
  , genesisAddress(_genesisAddress)
  , startAddress(_startAddress)
#ifdef SPAMMER
  , spammerAddress(_spammerAddress)
#endif
  , vector_datas()
  , m_pool()
  , v_pool()
  , b_pool()
  , fee_counter_()
{
#ifdef SPAMMER
  uint8_t sk[64];
  uint8_t pk[32];
  csdb::Address pub;
  for (int i = 0; i < NUM_OF_SPAM_KEYS; i++)
  {
    crypto_sign_keypair(pk, sk);
    pub = pub.from_public_key((const char*)pk);
    spam_keys.push_back(pub);
  }
#endif
}

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

void Solver::buildBlock(csdb::Pool& block)
{
  csdb::Transaction transaction;

  transaction.set_target(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000003"));
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));

  transaction.set_currency(csdb::Currency(1));
  transaction.set_amount(csdb::Amount(10, 0));
  transaction.set_balance(csdb::Amount(100, 0));
  transaction.set_innerID(0);

  block.add_transaction(transaction);

  transaction.set_target(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000004"));
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));

  transaction.set_currency(csdb::Currency(1));
  transaction.set_amount(csdb::Amount(10, 0));
  transaction.set_balance(csdb::Amount(100, 0));
  transaction.set_innerID(0);

  block.add_transaction(transaction);

}


void Solver::prepareBlockForSend(csdb::Pool& block)
{
  //std::cout << "SOLVER> Before time stamp" << std::endl;
//block is build in buildvector
  addTimestampToPool(block);
  //std::cout << "SOLVER> Before write pub key" << std::endl;
  block.set_writer_public_key(myPublicKey);
   //std::cout << "SOLVER> Before write last sequence" << std::endl;
  block.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);
  csdb::PoolHash prev_hash;
  prev_hash.from_string("");
 // block.set_previous_hash(prev_hash);
 // std::cout << "SOLVER> Before private key" << std::endl;
  block.sign(myPrivateKey);
#ifdef MYLOG
  std::cout << "last sequence: " << (node_->getBlockChain().getLastWrittenSequence()) << std::endl;// ", last time:" << node_->getBlockChain().loadBlock(node_->getBlockChain().getLastHash()).user_field(0).value<std::string>().c_str() 
  std::cout << "prev_hash: " << node_->getBlockChain().getLastHash().to_string() << " <- Not sending!!!" << std::endl;
  std::cout << "new sequence: " << block.sequence() << ", new time:" << block.user_field(0).value<std::string>().c_str() << std::endl;
  #endif
}




void Solver::sendTL()
{
  if (gotBigBang) return;
  size_t tNum = v_pool.transactions_count();
  std::cout << "AAAAAAAAAAAAAAAAAAAAAAAA -= TRANSACTION RECEIVING IS OFF =- AAAAAAAAAAAAAAAAAAAAAAAAAAAA" << std::endl;
 // std::cout << "                          Total received " << tNum << " transactions" << std::endl;
  std::cout << "========================================================================================" << std::endl;
  m_pool_closed = true;  
  std::cout << "Solver -> Sending " << tNum << " transactions " << std::endl;
  v_pool.set_sequence(node_->getRoundNumber());
  //std::cout << "Solver -> Sending TransactionList to ALL" << std::endl;//<< byteStreamToHex(it.str, 32)  //<< 
  node_->sendTransactionList(std::move(v_pool)); // Correct sending, better when to all one time

}

uint32_t Solver::getTLsize()
{
  return v_pool.transactions_count();
}


void Solver::setLastRoundTransactionsGot(size_t trNum)
{
  lastRoundTransactionsGot = trNum;
}

void Solver::closeMainRound()
{
  if (node_->getRoundNumber()==1)// || (lastRoundTransactionsGot==0)) //the condition of getting 0 transactions by previous main node should be added!!!!!!!!!!!!!!!!!!!!!
  //node_->sendFirstTransaction();
  
  {
      node_->becomeWriter();
#ifdef MYLOG
	std::cout << "Solver -> Node Level changed 2 -> 3" << std::endl;
#endif

    prepareBlockForSend(m_pool);

    b_pool.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);
    csdb::PoolHash prev_hash;
    prev_hash.from_string("");
    b_pool.set_previous_hash(prev_hash);
	
    std::cout << "Solver -> new sequence: " << m_pool.sequence() << ", new time:" << m_pool.user_field(0).value<std::string>().c_str() << std::endl;
 
    node_->sendBlock(std::move(m_pool));
    node_->sendBadBlock(std::move(b_pool));
	  std::cout << "Solver -> Block is sent ... awaiting hashes" << std::endl;

	  node_->getBlockChain().setGlobalSequence(m_pool.sequence());

#ifdef MYLOG
	std::cout << "Solver -> Global Sequence: "  << node_->getBlockChain().getGlobalSequence() << std::endl;
	std::cout << "Solver -> Writing New Block"<< std::endl;
#endif
    node_->getBlockChain().writeNewBlock(m_pool);
  }
}

bool Solver::mPoolClosed()
{
  return m_pool_closed;
}


void Solver::runMainRound()
{
  m_pool_closed = false;
  std::cout << "========================================================================================" << std::endl;
  std::cout << "VVVVVVVVVVVVVVVVVVVVVVVVV -= TRANSACTION RECEIVING IS ON =- VVVVVVVVVVVVVVVVVVVVVVVVVVVV" << std::endl;
 
  if(node_->getRoundNumber()==1) 
  {
    runAfter(std::chrono::milliseconds(2000),
      [this]() { closeMainRound(); });
  }
  else
  {

  runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS),
    [this]() { closeMainRound(); });
 }

}

const HashVector& Solver::getMyVector() const
{
  return hvector;
}

const HashMatrix& Solver::getMyMatrix() const
{
 return (generals->getMatrix());
}



void Solver::flushTransactions()
{
	if (node_->getMyLevel() != NodeLevel::Normal) { return; }
	{
    std::lock_guard<std::mutex> l(m_trans_mut);
    if (m_transactions.size()) {
      node_->sendTransaction(std::move(m_transactions));
      sentTransLastRound = true;
#ifdef MYLOG
	  std::cout << "FlushTransaction ..." << std::endl;
    #endif
      m_transactions.clear();
    } else {
      return;
    }
  }
  runAfter(std::chrono::milliseconds(50),
                  [this]() { flushTransactions(); });
}

bool Solver::getIPoolClosed() {
  return m_pool_closed;
}

void Solver::gotTransaction(csdb::Transaction&& transaction)
{
#ifdef MYLOG
	//std::cout << "SOLVER> Got Transaction" << std::endl;
#endif
	if (m_pool_closed)
	{
#ifdef MYLOG
		LOG_EVENT("m_pool_closed already, cannot accept your transactions");
#endif
		return;
	}

	if (transaction.is_valid())
	{
			v_pool.add_transaction(transaction);
	}
#ifdef MYLOG
	else
	{
		LOG_EVENT("Invalid transaction received");
	}
#endif
}

void Solver::initConfRound()
{
  memset(receivedVecFrom, 0, 100);
  memset(receivedMatFrom, 0, 100);
  trustedCounterVector = 0;
  trustedCounterMatrix = 0;
//  size_t _rNum = rNum;
  if (gotBigBang) sendZeroVector();
  //runAfter(std::chrono::milliseconds(TIME_TO_AWAIT_ACTIVITY),
  //  [this, _rNum]() { if(!transactionListReceived) node_->sendTLRequest(_rNum); });
}

void Solver::gotTransactionList(csdb::Pool&& _pool)
{
  if(transactionListReceived) return;
  transactionListReceived = true;
  uint8_t numGen = node_->getConfidants().size();
//	std::cout << "SOLVER> GotTransactionList" << std::endl;
  m_pool = csdb::Pool{};
  csdb::Pool pool = removeTransactionsWithBadSignatures(_pool);
  fee_counter_.CountFeesInPool(node_, &pool);
  Hash_ result = generals->buildvector(pool, m_pool, b_pool);
  receivedVecFrom[node_->getMyConfNumber()] = true;
	hvector.Sender = node_->getMyConfNumber();
	hvector.hash = result;
	receivedVecFrom[node_->getMyConfNumber()] = true;
	generals->addvector(hvector);
	node_->sendVector(std::move(hvector));
	++trustedCounterVector;
  if(trustedCounterVector==numGen) 
  {
    vectorComplete = true;

    memset(receivedVecFrom, 0, 100);
    trustedCounterVector = 0;
    //compose and send matrix!!!
    //receivedMat_ips.insert(node_->getMyId());
    generals->addSenderToMatrix(node_->getMyConfNumber());
    receivedMatFrom[node_->getMyConfNumber()] = true;
    ++trustedCounterMatrix;
    node_->sendMatrix(generals->getMatrix());
    generals->addmatrix(generals->getMatrix(), node_->getConfidants());//MATRIX SHOULD BE DECOMPOSED HERE!!!
#ifdef MYLOG
    std::cout << "SOLVER> Matrix added" << std::endl;
    #endif
  }
}

void Solver::sendZeroVector()
{

  if (transactionListReceived && !getBigBangStatus()) return;
  std::cout << "SOLVER> Generating ZERO TransactionList" << std::endl;
  csdb::Pool test_pool = csdb::Pool{};
  gotTransactionList(std::move(test_pool));

}


void Solver::gotVector(HashVector&& vector)
{
#ifdef MYLOG
	std::cout << "SOLVER> GotVector" << std::endl;
  #endif
 // runAfter(std::chrono::milliseconds(200),
 //   [this]() { sendZeroVector(); });

  uint8_t numGen = node_->getConfidants().size();
  //if (vector.roundNum==node_->getRoundNumber())
  //{
	 // std::cout << "SOLVER> This is not the information of this round" << std::endl;
	 // return;
  //}
  if (receivedVecFrom[vector.Sender]==true)
  {
#ifdef MYLOG
		std::cout << "SOLVER> I've already got the vector from this Node" << std::endl;
    #endif
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
 //   std::cout << "SOLVER> Matrix added" << std::endl;

    if (trustedCounterMatrix == numGen) takeDecWorkaround();
  }
#ifdef MYLOG
  std::cout << "Solver>  VECTOR GOT SUCCESSFULLY!!!" << std::endl;
  #endif
}

void Solver::takeDecWorkaround()
{
  memset(receivedMatFrom, 0, 100);
  trustedCounterMatrix = 0;
  uint8_t wTrusted = (generals->take_decision(node_->getConfidants(), node_->getMyConfNumber(), node_->getBlockChain().getHashBySequence(node_->getRoundNumber() - 1)));

  if (wTrusted == 100)
  {
    //        std::cout << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!" << std::endl;
    runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS),
      [this]() { writeNewBlock(); });
  }
  else
  {
    consensusAchieved = true;
    //       std::cout << "SOLVER> wTrusted = " << (int)wTrusted << std::endl;
    if (wTrusted == node_->getMyConfNumber())
    {
      node_->becomeWriter();
      runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS),
        [this]() { writeNewBlock(); });
    }
    //LOG_WARN("This should NEVER happen, NEVER");
  }
}



void Solver::checkMatrixReceived()
{
  if (trustedCounterMatrix < 2) node_->sendMatrix(generals->getMatrix());
  
}

void Solver::setRNum(size_t _rNum)
{
  rNum = _rNum;
}


void Solver::checkVectorsReceived(size_t _rNum)
{
  if (_rNum < rNum) return;
  uint8_t numGen = node_->getConfidants().size();
  if (trustedCounterVector == numGen) return;

}

void Solver::gotMatrix(HashMatrix&& matrix)
{
  //runAfter(std::chrono::milliseconds(500),
  //  [this]() { checkVectorsReceived(); });
	//std::cout << "SOLVER> Got Matrix" << std::endl;
	uint8_t numGen = node_->getConfidants().size();
  /*for(uint8_t i=0; i<numGen; i++)
  {
    if(!receivedVecFrom[i]) node_->sendVectorRequest(node_->getConfidants()[i]);
  }*/
  //if(trustedCounterMatrix==0)
  //{
  //      runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS/5),
  //      [this]() { writeNewBlock();});
  //}

  if(gotBlockThisRound) return;
	if (receivedMatFrom[matrix.Sender])
	{
#ifdef MYLOG
		std::cout << "SOLVER> I've already got the matrix from this Node" << std::endl;
#endif
		return;
	}
	receivedMatFrom[matrix.Sender] = true;
	trustedCounterMatrix++;
	generals->addmatrix(matrix, node_->getConfidants());
#ifdef MYLOG
  std::cout << "SOLVER> Matrix added" << std::endl;
#endif
  if (trustedCounterMatrix == numGen) takeDecWorkaround();
}

//what block does this function write???
void Solver::writeNewBlock()
{
#ifdef MYLOG
	std::cout << "Solver -> writeNewBlock ... start";
  #endif
  if (consensusAchieved &&
    node_->getMyLevel() == NodeLevel::Writer) {
    node_->getBlockChain().finishNewBlock(m_pool);
    prepareBlockForSend(m_pool);
    node_->sendBlock(m_pool);
    node_->getBlockChain().writeNewBlock(m_pool);
    node_->getBlockChain().setGlobalSequence(m_pool.sequence());
    b_pool.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);
    csdb::PoolHash prev_hash;
    prev_hash.from_string("");
    b_pool.set_previous_hash(prev_hash);

#ifdef MYLOG
	  std::cout << "Solver -> writeNewBlock ... finish" << std::endl;
#endif
	  consensusAchieved = false;
  }
  else {
    //LOG_WARN("Consensus achieved: " << (consensusAchieved ? 1 : 0) << ", ml=" << (int)node_->getMyLevel());
  }
}

void Solver::gotBlock(csdb::Pool&& block, const PublicKey& sender)
{
	if (node_->getMyLevel() == NodeLevel::Writer)
		return;
  gotBigBang = false;
  gotBlockThisRound = true;
#ifdef MONITOR_NODE
  addTimestampToPool(block);
#endif
  csdb::Pool::sequence_t g_seq = block.sequence();
#ifdef MYLOG
  std::cout << "GOT NEW BLOCK: global sequence = " << g_seq << std::endl;
  #endif
  if(g_seq > node_->getRoundNumber()) return; // remove this line when the block candidate signing of all trusted will be implemented
  
  node_->getBlockChain().setGlobalSequence(g_seq);
  if (g_seq == node_->getBlockChain().getLastWrittenSequence() + 1)
  {
		//std::cout << "Solver -> getblock calls writeLastBlock" << std::endl;
    if(block.verify_signature()) //INCLUDE SIGNATURES!!!
		{
      node_->getBlockChain().onBlockReceived(block);
      
		  if ((node_->getMyLevel() != NodeLevel::Writer) && (node_->getMyLevel() != NodeLevel::Main))
		  {
			  //std::cout << "Solver -> before sending hash to writer" << std::endl;
			  Hash test_hash((char*)(node_->getBlockChain().getLastWrittenHash().to_binary().data()));//getLastWrittenHash().to_binary().data()));//SENDING HASH!!!
			  node_->sendHash(test_hash, sender);
#ifdef MYLOG
        std::cout << "SENDING HASH: " << byteStreamToHex(test_hash.str,32) << std::endl;
#endif
		  }

    }

		//std::cout << "Solver -> finishing gotBlock" << std::endl;
  }
//  size_t _rNum = rNum;
 // runAfter(std::chrono::milliseconds(TIME_TO_AWAIT_ACTIVITY),
  //  [this, rNum]() { node_->sendRoundTableRequest(rNum); });

}

void Solver::gotIncorrectBlock(csdb::Pool&& block, const PublicKey& sender)
{
  std::cout << __func__ << std::endl;
  if (tmpStorage.count(block.sequence())==0) 
  {
    tmpStorage.emplace(block.sequence(), block);
    std::cout << "GOTINCORRECTBLOCK> block saved to temporary storage: " << block.sequence() << std::endl;
  }

}

void Solver::gotFreeSyncroBlock(csdb::Pool&& block)
{
  std::cout << __func__ << std::endl;
  if (rndStorage.count(block.sequence())==0) 
  {
    rndStorage.emplace(block.sequence(), block);
    std::cout << "GOTFREESYNCROBLOCK> block saved to temporary storage: " << block.sequence() << std::endl;
  }
}
void Solver::rndStorageProcessing()
{
  std::cout << __func__ << std::endl;
  bool loop = true;
  size_t newSeq;

  while(loop)
  {
    newSeq = node_->getBlockChain().getLastWrittenSequence() + 1;

    if (rndStorage.count(newSeq)>0) 
    {
      node_->getBlockChain().putBlock(rndStorage.at(newSeq));
      rndStorage.erase(newSeq);
    }
    else loop = false;
  }
}

void Solver::tmpStorageProcessing()
{
  std::cout << __func__ << std::endl;
  bool loop = true;
  size_t newSeq;

  while (loop)
  {
    newSeq = node_->getBlockChain().getLastWrittenSequence() + 1;

    if (tmpStorage.count(newSeq)>0)
    {
      node_->getBlockChain().putBlock(tmpStorage.at(newSeq));
      tmpStorage.erase(newSeq);
    }
    else loop = false;
  }
}


bool Solver::getBigBangStatus()
{
  return gotBigBang;
}

void Solver::setBigBangStatus(bool _status)
{
  gotBigBang = _status;
}


void Solver::gotBadBlockHandler(csdb::Pool&& _pool, const PublicKey& sender)
{
  //insert code here
}

void Solver::gotBlockCandidate(csdb::Pool&& block)
{
#ifdef MYLOG
	std::cout << "Solver -> getBlockCanditate" << std::endl;
  #endif
  if (blockCandidateArrived)
    return;

  //m_pool = std::move(block);

  blockCandidateArrived = true;
 // writeNewBlock();
}

void Solver::gotHash(Hash& hash, const PublicKey& sender)
{
	if (round_table_sent) return;
	//std::cout << "Solver -> gotHash: " << hash.to_string() << "from sender: " << sender.to_string() << std::endl;//<-debug feature
	Hash myHash((char*)(node_->getBlockChain().getLastWrittenHash().to_binary().data()));
#ifdef MYLOG
	std::cout << "Solver -> My Hash: " << byteStreamToHex(myHash.str,32) << std::endl;
#endif
	size_t ips_size = ips.size();
	if (ips_size <= min_nodes)
	{
		if (hash == myHash) 
		{
#ifdef MYLOG
			std::cout << "Solver -> Hashes are good" << std::endl;
#endif
			//hashes.push_back(hash);
			ips.push_back(sender);
		} 
		else
		{
#ifdef MYLOG
			if (hash != myHash) std::cout << "Hashes do not match!!!" << std::endl;
      #endif
			return;
		}
	}
	else
	{
#ifdef MYLOG
		std::cout << "Solver -> We have enough hashes!" << std::endl;
    #endif
		return;
	}

	
	if ((ips_size == min_nodes) && (!round_table_sent))
	{
		
#ifdef MYLOG
    std::cout << "Solver -> sending NEW ROUND table" << std::endl;
#endif
    node_->initNextRound(node_->getMyPublicKey(), std::move(ips));
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

#ifdef SPAMMER
void
Solver::spamWithTransactions()
{
	//if (node_->getMyLevel() != Normal) return;
  std::cout << "STARTING SPAMMER..." << std::endl;
  std::string mp = "1234567890abcdef";
  
  // std::string cachedBlock;
  // cachedBlock.reserve(64000);
  long counter = 0;
  uint64_t iid = 0;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  csdb::Transaction transaction;
  transaction.set_currency(csdb::Currency(1));

  while (true) {
    if (spamRunning && (node_->getMyLevel() == Normal))
    {
      {
          csdb::internal::WalletId id;

          if (node_->getBlockChain().findWalletId(spammerAddress, id)) {
            transaction.set_source(csdb::Address::from_wallet_id(id));
          } else {
            transaction.set_source(spammerAddress);
          }
          if (node_->getBlockChain().findWalletId(spam_keys[counter], id)) {
            transaction.set_target(csdb::Address::from_wallet_id(id));
          } else {
            transaction.set_target(spam_keys[counter]);
          }
          transaction.set_amount(csdb::Amount(randFT(1, 1000), 0));
          transaction.set_max_fee(csdb::AmountCommission(0.1));
          transaction.set_innerID(iid++);
  #ifdef MYLOG
         // std::cout << "Solver -> Transaction " << iid << " added" << std::endl;
          #endif
          {
          std::lock_guard<std::mutex> l(m_trans_mut);
          m_transactions.push_back(transaction);
          }
          if (counter++ == NUM_OF_SPAM_KEYS - 1) counter = 0;
      }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(TRX_SLEEP_TIME));
  }
}
#endif

///////////////////

void Solver::send_wallet_transaction(const csdb::Transaction& transaction)
{
  //TRACE("");
  std::lock_guard<std::mutex> l(m_trans_mut);
  //TRACE("");
  m_transactions.push_back(transaction);
}

void Solver::addInitialBalance()
{
#ifdef ADD_INITIAL_BALANCE
  std::cout << "===SETTING DB===" << std::endl;
  const std::string start_address =
    "0000000000000000000000000000000000000000000000000000000000000002";

  //csdb::Pool pool;
  csdb::Transaction transaction;
  transaction.set_target(
    csdb::Address::from_public_key((char*)myPublicKey.data()));
  transaction.set_source(csdb::Address::from_string(start_address));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(10000, 0));
  transaction.set_balance(csdb::Amount(10000000, 0));
  transaction.set_innerID(1);

  {
	  std::lock_guard<std::mutex> l(m_trans_mut);
	  m_transactions.push_back(transaction);
  }
#endif

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
#ifdef MYLOG
	std::cout << "Solver -> Got Block for my Request: " << pool.sequence() << std::endl;
  #endif
	if (pool.sequence() == node_->getBlockChain().getLastWrittenSequence() + 1)
		node_->getBlockChain().writeNewBlock(pool);
	

}

void Solver::addConfirmation(uint8_t confNumber_) {
  if(writingConfGotFrom[confNumber_]) return;
  writingConfGotFrom[confNumber_]=true;
  writingCongGotCurrent++;
  if(writingCongGotCurrent==2)
  {
    node_->becomeWriter();
    runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS),
      [this]() { writeNewBlock(); });
  } 
  
}

void Solver::nextRound()
{
#ifdef MYLOG
	std::cout << "SOLVER> next Round : Starting ... nextRound" << std::endl;
  #endif
  receivedVec_ips.clear();
  receivedMat_ips.clear();

  hashes.clear();
  ips.clear();
  vector_datas.clear();

  vectorComplete = false;
  consensusAchieved = false;
  blockCandidateArrived = false;
  transactionListReceived = false;
  vectorReceived = false;
  gotBlockThisRound=false;

  round_table_sent = false;
  sentTransLastRound = false;
  m_pool = csdb::Pool{};

  if (m_pool_closed) v_pool = csdb::Pool{};

  if(node_->getMyLevel() == NodeLevel::Confidant)
  {
    memset(receivedVecFrom, 0, 100);
    memset(receivedMatFrom, 0, 100);
    trustedCounterVector = 0;
    trustedCounterMatrix = 0;
    if (gotBigBang) sendZeroVector();
  }
#ifdef MYLOG
  std::cout << "SOLVER> next Round : the variables initialized" << std::endl;
  #endif
  if (node_->getMyLevel() == NodeLevel::Main) {
    runMainRound();

#ifdef SPAMMER
    spamRunning = false;
#endif
  } else {
#ifdef SPAMMER
    spamRunning = true;
#endif
  //  std::cout << "SOLVER> next Round : before flush transactions" << std::endl;
    m_pool_closed = true;
    flushTransactions();
  }
}

csdb::Pool Solver::removeTransactionsWithBadSignatures(const csdb::Pool& pool)
{
  csdb::Pool good_pool;
  std::vector<csdb::Transaction> transactions = pool.transactions();
  for (int i = 0; i < transactions.size(); ++i) {
    if (transactions[i].verify_signature())
      good_pool.add_transaction(transactions[i]);
  }
  return good_pool;
}

} // namespace Credits
