////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <sstream>
#include <string.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "Solver/Generals.hpp"
#include "Solver/WalletsState.h"

#include <algorithm>
#include <csdb/amount.h>
#include <csdb/amount_commission.h>
#include <csdb/transaction.h>
#include <csdb/pool.h>

#include <mutex>


namespace Credits{

    Generals::Generals(WalletsState& _walletsState)
        : walletsState(_walletsState)
    { }
    Generals::~Generals() { }

    Hash_ Generals::buildvector(csdb::Pool& _pool, csdb::Pool& new_pool, csdb::Pool& new_bpool) {
      ////////////////////////////////////////////////////////////////////////
      //    This function was modified to calculate deltas for concensus    //
      ////////////////////////////////////////////////////////////////////////
      #ifdef MYLOG
		std::cout << "GENERALS> buildVector: " << _pool.transactions_count() << " transactions"  << std::endl;
    #endif
      //comission is let to be constant, otherwise comission should be sent to this function
		memset(&hMatrix, 0, 9700);
    csdb::Transaction tempTransaction;
	  size_t transactionsNumber = _pool.transactions_count();
	  uint8_t* del1 = new uint8_t[transactionsNumber];
	  uint32_t i = 0;
  
    const csdb::Amount zero_balance = 0.0_c;
    if (_pool.transactions_count() > 0) {
      walletsState.updateFromSource();

	  std::vector <csdb::Transaction>& t_pool = _pool.transactions();
	  for (auto& it : t_pool)
	  {
      WalletsState::WalletId walletId{};
		  auto delta = walletsState.getData(it.source(), walletId).balance_  - it.amount() - it.counted_fee();

	#ifdef _MSC_VER
		  int8_t bitcnt = __popcnt(delta.integral()) + __popcnt64(delta.fraction());
	#else
		  int8_t bitcnt = __builtin_popcount(delta.integral()) + __builtin_popcountl(delta.fraction());
	#endif
      WalletsState::WalletData& wallState = walletsState.getData(it.source(), walletId);

      if (!wallState.trxTail_.isAllowed(it.innerID())) {
        *(del1 + i) = -bitcnt;
        new_bpool.add_transaction(it);
        ++i;
        continue;
      }

      if (delta <= zero_balance) {
        *(del1 + i) = -bitcnt;
        new_bpool.add_transaction(it);
        ++i;
        continue;
      }

      *(del1 + i) = bitcnt;
      wallState.trxTail_.push(it.innerID());
      walletsState.setModified(walletId);
      new_pool.add_transaction(it);
      ++i;
	  }
	  
		uint8_t* hash_s = new uint8_t[32];
		//std::cout << "GENERALS> Build vector : before blake" << std::endl;
  //  std::cout << "GENERALS> buildVector: before blake, total " << new_pool.transactions_count() << "transactions in block" << std::endl;
		blake2s(hash_s, 32, del1, transactionsNumber, "1234", 4);
//    std::cout << "GENERALS> buildVector: hash: " << byteStreamToHex((const char*)hash_s, 32) << " from " << (int)i << std::endl;
		//initializing for taking decision
		//std::cout << "GENERALS> Build vector : before initializing" << std::endl;
		memset(find_untrusted, 0, 10000);
		memset(new_trusted, 0, 100);
		memset(hw_total, 0, 3300);
		//std::cout << "GENERALS> Build vector : after zeroing" << std::endl;

		Hash_ hash_(hash_s);	
    delete hash_s; 
    delete del1;
 //   std::cout << "GENERALS> buildVector: hash in hash_: " << byteStreamToHex((const char*)hash_.val, 32) << std::endl;
		return hash_;
    }
    else
    {
      uint8_t* hash_s = new uint8_t[32];
      uint32_t a=0;
      blake2s(hash_s, 32, (const void*)&a, 4, "1234", 4);
//      std::cout << "GENERALS> buildVector: hash: " << byteStreamToHex((const char*)hash_s, 32) << " from " << (int)i << std::endl;
      memset(find_untrusted, 0, 10000);
      memset(new_trusted, 0, 100);
      memset(hw_total, 0, 3300);
      Hash_ hash_(hash_s);
      delete hash_s;
 //     std::cout << "GENERALS> buildVector: hash in hash_: " << byteStreamToHex((const char*)hash_.val, 32) << std::endl;
      delete del1;
      return hash_;

    }
    }

  void Generals::addvector(HashVector vector) {
  #ifdef MYLOG
  	std::cout << "GENERALS> Add vector" << std::endl;
  #endif
		hMatrix.hmatr[vector.Sender] = vector;
 //   std::cout << "GENERALS> Vector succesfully added" << std::endl;
  }

	void Generals::addSenderToMatrix(uint8_t myConfNum)
	{
		hMatrix.Sender = myConfNum;
	}

  void Generals::addmatrix(HashMatrix matrix, const std::vector<PublicKey>& confidantNodes) {
//		std::cout << "GENERALS> Add matrix" << std::endl;
		const uint8_t nodes_amount = confidantNodes.size();
		hash_weight *hw = new hash_weight[nodes_amount];
    Hash_ temp_hash;
		uint8_t j = matrix.Sender;
		uint8_t i_max;
		bool found = false;

		uint8_t max_frec_position;
		uint8_t j_max;
		j_max = 0;
    
 //   std::cout << "GENERALS> HW OUT: nodes amount = " << (int)nodes_amount <<  std::endl;
		for (uint8_t i = 0; i < nodes_amount; i++)
		{
			if (i == 0)
			{
				memcpy(hw[0].a_hash, matrix.hmatr[0].hash.val, 32);
  //      std::cout << "GENERALS> HW OUT: writing initial hash " << byteStreamToHex(hw[i].a_hash, 32) << std::endl;
				hw[0].a_weight = 1;
				*(find_untrusted + j * 100) = 0;
				i_max = 1;
			}
			else
			{
				found = false;
				for (uint8_t ii = 0; ii < i_max; ii++)
				{
   //       memcpy(temp_hash.val, matrix.hmatr[i].hash.val, 32);
   //      std::cout << "GENERALS> HW OUT: hash " << byteStreamToHex((const char*)temp_hash.val, 32) << " from " << (int)i << std::endl;
					if (memcmp(hw[ii].a_hash, matrix.hmatr[i].hash.val, 32) == 0)
					{
  //          std::cout << "GENERALS> HW OUT: hash found" ;
						(hw[ii].a_weight)++;
  //         std::cout << " ... now h_weight = " << (int)(hw[ii].a_weight) << std::endl;
						found = true;
						*(find_untrusted + j * 100 + i) = ii;
						break;
					}
				}
				if (!found)
				{
    //      std::cout << "GENERALS> HW OUT: hash not found!!!";
					memcpy(hw[i_max].a_hash, matrix.hmatr[i].hash.val, 32);
					(hw[i_max].a_weight) = 1;
					*(find_untrusted + j * 100 + i) = i_max;
					i_max++;
    //      std::cout << " ... i_max = "<< (int) i_max << std::endl;;


				}
			}
		}

	 uint8_t hw_max;
   hw_max=0;
   max_frec_position = 0;

		for (int i = 0; i < i_max; i++)
		{
			if (hw[i].a_weight > hw_max)
			{
				hw_max = hw[i].a_weight;
				max_frec_position = i;
   //     std::cout << "GENERALS> HW OUT:" << i << " : " << byteStreamToHex(hw[i].a_hash, 32) << " - > " << (int)(hw[i].a_weight) << std::endl;

			}
		}
    j = matrix.Sender;
    hw_total[j].a_weight=max_frec_position;
    memcpy(hw_total[j].a_hash, hw[max_frec_position].a_hash,32);

		for (int i = 0; i < nodes_amount; i++)
		{
			if (*(find_untrusted + i + j * 100) == max_frec_position)
			{
				*(new_trusted + i) += 1;
			}
		}
    delete hw;
    }

    uint8_t Generals::take_decision(const std::vector<PublicKey>& confidantNodes, const uint8_t myConfNumber, const csdb::PoolHash lasthash) {
    #ifdef MYLOG
		std::cout << "GENERALS> Take decision: starting " << std::endl;
    #endif
		const uint8_t nodes_amount = confidantNodes.size();
		hash_weight *hw = new hash_weight[nodes_amount];
		unsigned char *mtr = new unsigned char[nodes_amount * 97];

		uint8_t max_frec_position;
		uint8_t j_max, jj;
		j_max = 0;
    bool found;
		
		//double duration, duration1;
		//duration1 = 0;
		//clock_t time_begin, time_end, time_begin1, time_end1;
		//time_begin = clock();

      memset(mtr, 0, nodes_amount * 97);		
		for (int j = 0; j < nodes_amount; j++)
		{
			//time_begin1 = clock();
      // matrix init
			//create_matrix(mtr, nodes_amount);// matrix generation
			//time_end1 = clock();
			//duration1 += time_end1 - time_begin1;
      
			//primary matrix check
			//hw_total[j].a_weight = 0;
			//final check

			if (j == 0)
			{
				memcpy(hw[0].a_hash, hw_total[0].a_hash, 32);
				(hw[0].a_weight) = 1;
				j_max = 1;
			}
			else
			{
				found = false;
				for (jj = 0; jj < j_max; jj++)
				{
					if (memcmp(hw[jj].a_hash, hw_total[j].a_hash, 32) == 0)
					{
						(hw[jj].a_weight)++;
						found = true;
						break;
					}
				}

				if (!found)
				{
					memcpy(hw[j_max].a_hash, hw_total[j].a_hash, 32);
					(hw_total[j_max].a_weight) = 1;
					j_max++;
				}
			}

		}


		uint8_t trusted_limit;
		trusted_limit = nodes_amount / 2 + 1;
    uint8_t j=0;
		for (int i = 0; i < nodes_amount; i++)
		{
      if (*(new_trusted + i) < trusted_limit)
      {
      #ifdef MYLOG
		  std::cout << "GENERALS> Take decision: Liar nodes : " << i << std::endl;
      #endif
      }
      else j++;
		}
    if (j==nodes_amount) 
    { 
      #ifdef MYLOG
      std::cout << "GENERALS> Take decision: CONGRATULATIONS!!! No liars this round!!! " << std::endl;
      #endif
    }
#ifdef MYLOG
    std::cout << "Hash : " << lasthash.to_string() << std::endl;
    #endif
		auto hash_t = lasthash.to_binary();
		int k= *(hash_t.begin());
		//std::cout << "K : " << k << std::endl;
		int result0 = nodes_amount;
		uint16_t result =0;
		result = k % (int)result0;
#ifdef MYLOG
		std::cout << "Writing node : " << byteStreamToHex(confidantNodes.at(result).str,32) << std::endl;
    #endif
    delete hw;
    delete mtr;
		return result;
		//if (myId != confidantNodes[write_id]) return 0;
        //return 100;
    }

    HashMatrix Generals::getMatrix() {
       return hMatrix;
    }

	void Generals::chooseHeadAndTrusted(std::map<std::string, std::string>) { }
    void Generals::chooseHeadAndTrustedFake(std::vector<std::string>& hashes) { }
    void Generals::fake_block(std::string m_public_key) { }

void Generals::countFee(csdb::Transaction& transaction, size_t numOfTrustedNodesInRound,
	size_t numOfTransactionsInRound)
{
	constexpr int NUM_OF_ROUDS_PER_SECOND = 5;
	constexpr double k = COST_OF_ONE_TRUSTED_PER_DAY / (24 * 3600 * NUM_OF_ROUDS_PER_SECOND);
	double fee = numOfTrustedNodesInRound * k / numOfTransactionsInRound;

	if (size_t transaction_size = transaction.to_byte_stream().size() > SIZE_OF_COMMON_TRANSACTION)
	{
		double l = 1. / SIZE_OF_COMMON_TRANSACTION;
		double lengthCoef = sqrt(transaction_size * l);
		lengthCoef = lengthCoef * lengthCoef * lengthCoef;
		fee *= lengthCoef;
	}

	transaction.set_counted_fee(csdb::AmountCommission(fee));
}

}