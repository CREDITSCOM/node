////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstring>
#include <iostream>
#include <blake2-impl.h>
#include <blake2.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include <csdb/currency.h>
#include <csdb/pool.h>
#include <csdb/transaction.h>
#include <algorithm>
#include <boost/dynamic_bitset.hpp>
#include <lib/system/utils.hpp>

#include <mutex>
#include <solver/generals.hpp>
#include <solver/WalletsState.h>
#include <csnode/conveyer.hpp>

namespace cs {
Generals::Generals(WalletsState& _walletsState)
  : m_walletsState(_walletsState)
  , m_transactionsValidator(new TransactionsValidator(m_walletsState, TransactionsValidator::Config {}))
  {}

int8_t Generals::extractRaisedBitsCount(const csdb::Amount& delta) {
#ifdef _MSC_VER
  return static_cast<int8_t>(__popcnt(delta.integral()) + __popcnt64(delta.fraction()));
#else
  return static_cast<int8_t>(__builtin_popcount(delta.integral()) + __builtin_popcountl(delta.fraction()));
#endif
}

void Generals::resetHashMatrix() {
  std::memset(&m_hMatrix, 0, sizeof(m_hMatrix));
}

cs::Hash Generals::buildVector(const cs::TransactionsPacket& packet, Solver* solver) {
  cslog() << "GENERALS> buildVector: " << packet.transactionsCount() << " transactions";

  cs::Hash hash;

  const std::size_t transactionsCount = packet.transactionsCount();
  const auto& transactions = packet.transactions();

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  cs::Characteristic characteristic;

  if (transactionsCount > 0) {
    m_walletsState.updateFromSource();
    m_transactionsValidator->reset(transactionsCount);

    cs::Bytes characteristicMask;
    characteristicMask.reserve(transactionsCount);

    uint8_t del1;
    csdb::Pool newPool;

    for (std::size_t i = 0; i < transactionsCount; ++i) {
      const csdb::Transaction& transaction = transactions[i];
      cs::Byte byte = static_cast<cs::Byte>(m_transactionsValidator->validateTransaction(transaction, i, del1));

      if (byte) {
        byte = static_cast<cs::Byte>(solver->checkTransactionSignature(transaction));
      }

      characteristicMask.push_back(byte);
    }

    m_transactionsValidator->validateByGraph(characteristicMask, packet.transactions(), newPool);

    characteristic.mask = std::move(characteristicMask);
    conveyer.setCharacteristic(characteristic);
  }
  else {
    conveyer.setCharacteristic(characteristic);
  }

  if (characteristic.mask.size() != transactionsCount) {
    cserror() << "GENERALS> Build vector, characteristic mask size not equals transactions count";
  }

  blake2s(hash.data(), hash.size(), characteristic.mask.data(), characteristic.mask.size(), nullptr, 0u);

  m_findUntrusted.fill(0);
  m_newTrusted.fill(0);
  m_hwTotal.fill(HashWeigth{});

  csdebug() << "GENERALS> Generated hash: " << cs::Utils::byteStreamToHex(hash.data(), hash.size());

  return hash;
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

  const size_t nodes_amount = confidantNodes.size();
  constexpr uint8_t confidantsCountMax = TrustedSize + 1;  // from technical paper
  assert(nodes_amount <= confidantsCountMax);

  std::array<HashWeigth, confidantsCountMax> hw;
  uint8_t j = matrix.sender;
  uint8_t i_max = 0;
  bool found = false;

  uint8_t max_frec_position;

  cslog() << "GENERALS> HW OUT: nodes amount = " << nodes_amount;

  for (uint8_t i = 0; i < nodes_amount; i++) {
    if (i == 0) {
      hw[0].hash = matrix.hashVector[0].hash;

      const auto& hash = hw[i].hash;
      cslog() << "GENERALS> HW OUT: writing initial hash " << cs::Utils::byteStreamToHex(hash.data(), hash.size());

      hw[0].weight = 1;
      *(m_findUntrusted.data() + j * TrustedSize) = 0;
      i_max = 1;
    }
    else {
      found = false;

      // FIXME: i_max uninitialized in this branch!!!!
      for (uint8_t ii = 0; ii < i_max; ii++) {
        if (hw[ii].hash == matrix.hashVector[i].hash) {
          (hw[ii].weight)++;
          *(m_findUntrusted.data() + j * TrustedSize + i) = ii;

          found = true;
          break;
        }
      }

      if (!found) {
        hw[i_max].hash = matrix.hashVector[i].hash;

        (hw[i_max].weight) = 1;
        *(m_findUntrusted.data() + j * TrustedSize + i) = i_max;

        i_max++;

      }
    }
  }

  uint8_t hw_max = 0;
  max_frec_position = 0;

  for (int i = 0; i < i_max; i++) {
    if (hw[i].weight > hw_max) {
      hw_max = hw[i].weight;
      max_frec_position = cs::numeric_cast<uint8_t>(i);
    }
  }

  j = matrix.sender;
  m_hwTotal[j].weight = max_frec_position;
  m_hwTotal[j].hash = hw[max_frec_position].hash;

  for (size_t i = 0; i < nodes_amount; i++) {
    if (*(m_findUntrusted.data() + i + j * TrustedSize) == max_frec_position) {
      ++(*(m_newTrusted.data() + i));
    }
  }
}

uint8_t Generals::takeDecision(const cs::ConfidantsKeys& confidantNodes, const csdb::PoolHash& lasthash) {
  csdebug() << "GENERALS> Take decision: starting ";

  const uint8_t nodes_amount = static_cast<uint8_t>(confidantNodes.size());
  std::vector<HashWeigth> hash_weights(nodes_amount, HashWeigth());

  uint8_t j_max, jj;
  j_max = 0;

  for (uint8_t j = 0; j < nodes_amount; j++) {
    // matrix init
    if (j == 0) {
      hash_weights[0].hash = m_hwTotal[0].hash;
      (hash_weights[0].weight) = 1;
      j_max                      = 1;
    } else {
      bool found = false;

      for (jj = 0; jj < j_max; jj++) {
        if (hash_weights[jj].hash == m_hwTotal[j].hash) {
          (hash_weights[jj].weight)++;
          found = true;

          break;
        }
      }

      if (!found) {
        hash_weights[j_max].hash = m_hwTotal[j].hash;

        (m_hwTotal[j_max].weight) = 1;
        j_max++;
      }
    }
  }

  uint8_t trusted_limit = nodes_amount / 2 + 1;
  uint8_t j = 0;

  for (int i = 0; i < nodes_amount; i++) {
    if (*(m_newTrusted.data() + i) < trusted_limit) {
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

  return cs::numeric_cast<uint8_t>(result);
}

const HashMatrix& Generals::getMatrix() const {
  return m_hMatrix;
}

const PublicKey& Generals::getWriterPublicKey() const {
  return m_writerPublicKey;
}
}  // namespace cs
