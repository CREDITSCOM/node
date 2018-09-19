////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <iostream>
#include <sstream>
#include <string>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include <boost/dynamic_bitset.hpp>
#include "Solver/Generals.hpp"
//#include "../../../Include/Solver/Fake_Solver.hpp"

#include <csdb/address.h>
#include <csdb/currency.h>
#include <csdb/pool.h>
#include <csdb/transaction.h>
#include <algorithm>

#include <mutex>

namespace Credits {

Generals::Generals() {
}
Generals::~Generals() {
}

int8_t Generals::extractRaisedBitsCount(const csdb::Amount& delta) {
#ifdef _MSC_VER
  return __popcnt(delta.integral()) + __popcnt64(delta.fraction());
#else
  return __builtin_popcount(delta.integral()) + __builtin_popcountl(delta.fraction());
#endif
}

Hash_ Generals::buildvector(csdb::Pool& _pool, csdb::Pool& new_pool) {
  //    This function was modified to calculate deltas for concensus
 std::cout << "GENERALS> buildVector: " << _pool.transactions_count() << " transactions" << std::endl;
  // comission is let to be constant, otherwise comission should be sent to this function

  memset(&m_hMatrix, 0, 9700);
  uint8_t      hash_s[HASH_LENGTH] = {};  // if is array type, each element is zero-initialized en.cppreference.com
  const size_t transactionsCount   = _pool.transactions_count();
  if (transactionsCount > 0) {
    const csdb::Amount comission    = 0.1_c;
    const csdb::Amount zero_balance = 0.0_c;

    boost::dynamic_bitset<> characteristicMask{transactionsCount};

    for (size_t i = 0; i < transactionsCount; ++i) {
      const csdb::Transaction& transaction = _pool.transactions().at(i);
      const csdb::Amount       delta       = transaction.balance() - transaction.amount() - comission;

      if (delta > zero_balance) {
        characteristicMask.set(i, true);
        new_pool.add_transaction(transaction);
      }
    }

    boost::to_block_range(characteristicMask, std::back_inserter(m_characteristic_mask));
    m_characteristic_mask.shrink_to_fit();
    blake2s(&hash_s, HASH_LENGTH, m_characteristic_mask.data(), transactionsCount, "1234", 4);
  } else {
    uint32_t a = 0;
    blake2s(&hash_s, HASH_LENGTH, static_cast<const void*>(&a), 4, "1234", 4);
  }
  m_find_untrusted.fill(0);
  m_new_trusted.fill(0);
  m_hw_total.fill(hash_weight{});
  Hash_ hash_(hash_s);
  return hash_;
}

void Generals::addvector(HashVector vector) {
  std::cout << "GENERALS> Add vector" << std::endl;

  m_hMatrix.hmatr[vector.Sender] = vector;
  std::cout << "GENERALS> Vector succesfully added" << std::endl;
}

void Generals::addSenderToMatrix(uint8_t myConfNum) {
  m_hMatrix.Sender = myConfNum;
}

void Generals::addmatrix(HashMatrix matrix, const std::vector<PublicKey>& confidantNodes) {
 std::cout << "GENERALS> Add matrix" << std::endl;
  const uint8_t nodes_amount = confidantNodes.size();
  hash_weight*  hw           = new hash_weight[nodes_amount];
  Hash_         temp_hash;
  uint8_t       j = matrix.Sender;
  uint8_t       i_max;
  bool          found = false;

  uint8_t max_frec_position;
  uint8_t j_max;
  j_max = 0;

 std::cout << "GENERALS> HW OUT: nodes amount = " << (int)nodes_amount << std::endl;
  for (uint8_t i = 0; i < nodes_amount; i++) {
    if (i == 0) {
      memcpy(hw[0].a_hash, matrix.hmatr[0].hash.val, 32);
     std::cout << "GENERALS> HW OUT: writing initial hash " << byteStreamToHex(hw[i].a_hash, 32)
                               << std::endl;
      hw[0].a_weight                = 1;
      *(m_find_untrusted.data() + j * 100) = 0;
      i_max                         = 1;
    } else {
      found = false;
      for (uint8_t ii = 0; ii < i_max; ii++) {
        if (memcmp(hw[ii].a_hash, matrix.hmatr[i].hash.val, 32) == 0) {
          (hw[ii].a_weight)++;

          found                             = true;
          *(m_find_untrusted.data() + j * 100 + i) = ii;
          break;
        }
      }
      if (!found) {

        memcpy(hw[i_max].a_hash, matrix.hmatr[i].hash.val, 32);
        (hw[i_max].a_weight)              = 1;
        *(m_find_untrusted.data() + j * 100 + i) = i_max;
        i_max++;

      }
    }
  }

  uint8_t hw_max;
  hw_max            = 0;
  max_frec_position = 0;

  for (int i = 0; i < i_max; i++) {
    if (hw[i].a_weight > hw_max) {
      hw_max            = hw[i].a_weight;
      max_frec_position = i;
    }
  }
  j                      = matrix.Sender;
  m_hw_total[j].a_weight = max_frec_position;
  memcpy(m_hw_total[j].a_hash, hw[max_frec_position].a_hash, 32);

  for (int i = 0; i < nodes_amount; i++) {
    if (*(m_find_untrusted.data() + i + j * 100) == max_frec_position) {
      *(m_new_trusted.data() + i) += 1;
    }
  }
  delete[] hw;
}

uint8_t Generals::take_decision(const std::vector<PublicKey>& confidantNodes, const csdb::PoolHash& lasthash) {
#ifdef MYLOG
  std::cout << "GENERALS> Take decision: starting " << std::endl;
#endif
  const uint8_t nodes_amount = confidantNodes.size();
  auto          hash_weights = new hash_weight[nodes_amount];
  auto          mtr          = new unsigned char[nodes_amount * 97];

  uint8_t j_max, jj;
  j_max = 0;
  bool found;

  memset(mtr, 0, nodes_amount * 97);
  for (uint j = 0; j < nodes_amount; j++) {
    // matrix init

    if (j == 0) {
      memcpy(hash_weights[0].a_hash, m_hw_total[0].a_hash, 32);
      (hash_weights[0].a_weight) = 1;
      j_max            = 1;
    } else {
      found = false;
      for (jj = 0; jj < j_max; jj++) {
        if (memcmp(hash_weights[jj].a_hash, m_hw_total[j].a_hash, 32) == 0) {
          (hash_weights[jj].a_weight)++;
          found = true;
          break;
        }
      }

      if (!found) {
        memcpy(hash_weights[j_max].a_hash, m_hw_total[j].a_hash, 32);
        (m_hw_total[j_max].a_weight) = 1;
        j_max++;
      }
    }
  }

  uint8_t trusted_limit;
  trusted_limit = nodes_amount / 2 + 1;
  uint8_t j     = 0;
  for (int i = 0; i < nodes_amount; i++) {
    if (*(m_new_trusted.data() + i) < trusted_limit) {
     std::cout << "GENERALS> Take decision: Liar nodes : " << i << std::endl;

    } else {
      j++;
      }
  }
  if (j == nodes_amount) {
   std::cout << "GENERALS> Take decision: CONGRATULATIONS!!! No liars this round!!! " << std::endl;
  }

 std::cout << "Hash : " << lasthash.to_string() << std::endl;

  auto hash_t = lasthash.to_binary();
  int  k      = *(hash_t.begin());

  uint16_t result  = 0;
  result           = k % nodes_amount;

  std::cout << "Writing node : " << byteStreamToHex(confidantNodes.at(result).str, 32) << std::endl;

  delete[] hash_weights;
  delete[] mtr;
  return result;
}

HashMatrix Generals::getMatrix() const {
  return m_hMatrix;
}

void Generals::chooseHeadAndTrusted(std::map<std::string, std::string>) {
}
void Generals::chooseHeadAndTrustedFake(std::vector<std::string>& hashes) {
}
void Generals::fake_block(std::string m_public_key) {
}

std::vector<uint8_t> Generals::getCharacteristicMask() const {
  return m_characteristic_mask;
}
}  // namespace Credits
