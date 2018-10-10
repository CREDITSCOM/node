////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include <csdb/address.h>
#include <csdb/currency.h>
#include <csdb/pool.h>
#include <csdb/transaction.h>
#include <algorithm>
#include <boost/dynamic_bitset.hpp>
#include <lib/system/utils.hpp>

#include <mutex>
#include "solver/generals.hpp"

namespace cs {
int8_t Generals::extractRaisedBitsCount(const csdb::Amount& delta) {
#ifdef _MSC_VER
  return __popcnt(delta.integral()) + __popcnt64(delta.fraction());
#else
  return __builtin_popcount(delta.integral()) + __builtin_popcountl(delta.fraction());
#endif
}

cs::Hash Generals::buildVector(cs::TransactionsPacket& packet, csdb::Pool& new_pool) {
  cslog() << "GENERALS> buildVector: " << packet.transactionsCount() << " transactions";

  std::memset(&m_hMatrix, 0, sizeof(m_hMatrix));

  uint8_t      hash_s[HASH_LENGTH] = {};  // if is array type, each element is zero-initialized en.cppreference.com
  const size_t transactionsCount   = packet.transactionsCount();

  if (transactionsCount > 0) {
    const csdb::Amount comission    = 0.1_c;
    const csdb::Amount zero_balance = 0.0_c;

    boost::dynamic_bitset<> characteristicMask { transactionsCount };

    for (size_t i = 0; i < transactionsCount; ++i) {
      const csdb::Transaction& transaction = packet.transactions().at(i);
      const csdb::Amount       delta       = transaction.balance() - transaction.amount() - comission;

      if (delta > zero_balance) {
        characteristicMask.set(i, true);
        new_pool.add_transaction(transaction);
      }
    }

    m_characteristic.size = static_cast<uint32_t>(transactionsCount);

    cs::Bytes serializedCahracteristicMask;
    boost::to_block_range(characteristicMask, std::back_inserter(serializedCahracteristicMask));

    serializedCahracteristicMask.shrink_to_fit();
    m_characteristic.mask = std::move(serializedCahracteristicMask);

    blake2s(&hash_s, HASH_LENGTH, m_characteristic.mask.data(), transactionsCount, "1234", 4);
  } else {
    uint32_t a = 0;
    blake2s(&hash_s, HASH_LENGTH, static_cast<const void*>(&a), 4, "1234", 4);
  }

  m_find_untrusted.fill(0);
  m_new_trusted.fill(0);
  m_hw_total.fill(HashWeigth{});

  cs::Hash result;
  std::copy(hash_s, hash_s + HASH_LENGTH, result.begin());

  return result;
}

void Generals::addVector(const HashVector& vector) {
  cslog() << "GENERALS> Add vector";

  m_hMatrix.hashVector[vector.sender] = vector;
  cslog() << "GENERALS> Vector succesfully added";
}

void Generals::addSenderToMatrix(uint8_t myConfNum) {
  m_hMatrix.sender = myConfNum;
}

void Generals::addMatrix(const HashMatrix& matrix, const cs::ConfidantsKeys& confidantNodes) {
  cslog() << "GENERALS> Add matrix";

  const uint8_t nodes_amount = static_cast<uint8_t>(confidantNodes.size());

  auto*   hw = new HashWeigth[nodes_amount];
  cs::Hash temp_hash;
  uint8_t j = matrix.sender;
  uint8_t i_max;
  bool    found = false;

  uint8_t max_frec_position;

  cslog() << "GENERALS> HW OUT: nodes amount = " << int(nodes_amount);

  for (uint8_t i = 0; i < nodes_amount; i++) {
    if (i == 0) {
      memcpy(hw[0].hash, matrix.hashVector[0].hash.data(), matrix.hashVector[0].hash.size());

      cslog() << "GENERALS> HW OUT: writing initial hash " << cs::Utils::byteStreamToHex(hw[i].hash, 32);

      hw[0].weight                       = 1;
      *(m_find_untrusted.data() + j * 100) = 0;
      i_max                                = 1;
    } else {
      found = false;

      for (uint8_t ii = 0; ii < i_max; ii++) {
        if (memcmp(hw[ii].hash, matrix.hashVector[i].hash.data(), matrix.hashVector[i].hash.size()) == 0) {
          (hw[ii].weight)++;
          *(m_find_untrusted.data() + j * 100 + i) = ii;

          found = true;
          break;
        }
      }

      if (!found) {
        memcpy(hw[i_max].hash, matrix.hashVector[i].hash.data(), matrix.hashVector[i].hash.size());

        (hw[i_max].weight)                     = 1;
        *(m_find_untrusted.data() + j * 100 + i) = i_max;

        i_max++;

      }
    }
  }

  uint8_t hw_max    = 0;
  max_frec_position = 0;

  for (int i = 0; i < i_max; i++) {
    if (hw[i].weight > hw_max) {
      hw_max            = hw[i].weight;
      max_frec_position = i;
    }
  }

  j                      = matrix.sender;
  m_hw_total[j].weight = max_frec_position;

  memcpy(m_hw_total[j].hash, hw[max_frec_position].hash, 32);

  for (int i = 0; i < nodes_amount; i++) {
    if (*(m_find_untrusted.data() + i + j * 100) == max_frec_position) {
      *(m_new_trusted.data() + i) += 1;
    }
  }

  delete[] hw;
}

uint8_t Generals::takeDecision(const cs::ConfidantsKeys& confidantNodes, const csdb::PoolHash& lasthash) {
  csdebug() << "GENERALS> Take decision: starting ";

  const uint8_t nodes_amount = static_cast<uint8_t>(confidantNodes.size());
  auto hash_weights = new HashWeigth[nodes_amount];
  auto mtr = new unsigned char[nodes_amount * 97];      // what is 97 magic value?

  uint8_t j_max, jj;
  j_max = 0;

  std::memset(mtr, 0, nodes_amount * 97);

  for (uint8_t j = 0; j < nodes_amount; j++) {
    // matrix init
    if (j == 0) {
      memcpy(hash_weights[0].hash, m_hw_total[0].hash, 32);
      (hash_weights[0].weight) = 1;
      j_max                      = 1;
    } else {
      bool found = false;

      for (jj = 0; jj < j_max; jj++) {
        if (memcmp(hash_weights[jj].hash, m_hw_total[j].hash, 32) == 0) {
          (hash_weights[jj].weight)++;
          found = true;

          break;
        }
      }

      if (!found) {
        std::memcpy(hash_weights[j_max].hash, m_hw_total[j].hash, 32);

        (m_hw_total[j_max].weight) = 1;
        j_max++;
      }
    }
  }

  uint8_t trusted_limit;
  trusted_limit = nodes_amount / 2 + 1;

  uint8_t j = 0;

  for (int i = 0; i < nodes_amount; i++) {
    if (*(m_new_trusted.data() + i) < trusted_limit) {
      cslog() << "GENERALS> Take decision: Liar nodes : " << i;
    } else {
      j++;
    }
  }

  if (j == nodes_amount) {
    cslog() << "GENERALS> Take decision: CONGRATULATIONS!!! No liars this round!!! ";
  }

  cslog() << "Hash : " << lasthash.to_string();

  auto hash_t = lasthash.to_binary();
  int k = *(hash_t.begin());

  uint16_t result = k % nodes_amount;

  m_writerPublicKey = confidantNodes.at(result);

  cslog() << "Writing node : " << cs::Utils::byteStreamToHex(m_writerPublicKey.data(), m_writerPublicKey.size());

  delete[] hash_weights;
  delete[] mtr;

  return result;
}

const HashMatrix& Generals::getMatrix() const {
  return m_hMatrix;
}

const Characteristic& Generals::getCharacteristic() const {
  return m_characteristic;
}

const PublicKey& Generals::getWriterPublicKey() const {
  return m_writerPublicKey;
}
}  // namespace cs
