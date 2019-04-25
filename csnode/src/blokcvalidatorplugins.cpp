#include <csnode/blockvalidatorplugins.hpp>

#include <string>
#include <algorithm>
#include <set>

#include <csdb/pool.hpp>
#include <csnode/blockchain.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/common.hpp>
#include <csnode/walletsstate.hpp>
#include <csnode/walletscache.hpp>
#include <csdb/amount_commission.hpp>
#include <csdb/pool.hpp>
#include <cscrypto/cscrypto.hpp>
#include <smartcontracts.hpp>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace {
const char* kLogPrefix = "BlockValidator: ";
const cs::Sequence kGapBtwNeighbourBlocks = 1;
const csdb::user_field_id_t kTimeStampUserFieldNum = 0;
const uint8_t kBlockVerToSwitchCountedFees = 0;
} // namespace

namespace cs {

ValidationPlugin::ErrorType HashValidator::validateBlock(const csdb::Pool& block) {
  auto prevHash = block.previous_hash();
  auto& prevBlock = getPrevBlock();
  auto data = prevBlock.to_binary();
  auto countedPrevHash = csdb::PoolHash::calc_from_data(cs::Bytes(data.data(),
                                                          data.data() +
                                                          prevBlock.hashingLength()));
  if (prevHash != countedPrevHash) {
    csfatal() << kLogPrefix << ": prev pool's (" << prevBlock.sequence()
              << ") hash != real prev pool's hash";
    return ErrorType::fatalError;      
  }
  return ErrorType::noError;
}

ValidationPlugin::ErrorType BlockNumValidator::validateBlock(const csdb::Pool& block) {
  auto& prevBlock = getPrevBlock();
  if (block.sequence() - prevBlock.sequence() != kGapBtwNeighbourBlocks) {
    cserror() << kLogPrefix << "Current block's sequence is " << block.sequence()
              << ", previous block sequence is " << prevBlock.sequence();
    return ErrorType::error;
  }
  return ErrorType::noError;
}

ValidationPlugin::ErrorType TimestampValidator::validateBlock(const csdb::Pool& block) {
  auto& prevBlock = getPrevBlock();

  auto prevBlockTimestampUf = prevBlock.user_field(kTimeStampUserFieldNum);
  if (!prevBlockTimestampUf.is_valid()) {
    cswarning() << kLogPrefix << "Block with sequence " << prevBlock.sequence() << " has no timestamp";
    return ErrorType::warning;
  }
  auto currentBlockTimestampUf = block.user_field(kTimeStampUserFieldNum);
  if (!currentBlockTimestampUf.is_valid()) {
    cswarning() << kLogPrefix << "Block with sequence " << block.sequence() << " has no timestamp";
    return ErrorType::warning;
  }

  auto prevBlockTimestamp = std::stoll(prevBlockTimestampUf.value<std::string>());
  auto currentBlockTimestamp = std::stoll(currentBlockTimestampUf.value<std::string>());
  if (currentBlockTimestamp < prevBlockTimestamp) {
    cswarning() << kLogPrefix << "Block with sequence " << block.sequence()
                << " has timestamp " << currentBlockTimestamp
                << " less than " << prevBlockTimestamp
                << " in block with sequence " << prevBlock.sequence();
    return ErrorType::warning;
  }
  return ErrorType::noError;
}

ValidationPlugin::ErrorType BlockSignaturesValidator::validateBlock(const csdb::Pool& block) {
  uint64_t realTrustedMask = block.realTrusted();
#ifdef _MSC_VER
  size_t numOfRealTrusted = static_cast<decltype(numOfRealTrusted)>(__popcnt64(realTrustedMask));
#else
  size_t numOfRealTrusted = static_cast<decltype(numOfRealTrusted)>(__builtin_popcountl(realTrustedMask));
#endif

  auto signatures = block.signatures();
  if (signatures.size() != numOfRealTrusted) {
    cserror() << kLogPrefix << "in block " << block.sequence()
              << " num of signatures (" << signatures.size()
              << ") != num of real trusted (" << numOfRealTrusted << ")";
    return ErrorType::error;
  }

  auto confidants = block.confidants();
  const size_t maxTrustedNum = sizeof(realTrustedMask) * 8;
  if (confidants.size() > maxTrustedNum) {
    cserror() << kLogPrefix << "in block " << block.sequence()
              << " num of confidants " << confidants.size()
              << " is greated than max bits in realTrustedMask";
    return ErrorType::error;
  }

  size_t checkingSignature = 0;
  auto signedData = cscrypto::calculateHash(block.to_binary().data(), block.hashingLength());
  for (size_t i = 0; i < confidants.size(); ++i) {
    if (realTrustedMask & (1ull << i)) {
      if (!cscrypto::verifySignature(signatures[checkingSignature],
                                     confidants[i],
                                     signedData.data(),
                                     cscrypto::kHashSize)) {
        cserror() << kLogPrefix << "block " << block.sequence()
                  << " has invalid signatures";
        return ErrorType::error;
      }
      ++checkingSignature;
    }
  }

  return ErrorType::noError;
}

ValidationPlugin::ErrorType SmartSourceSignaturesValidator::validateBlock(const csdb::Pool& block) {
  const auto& transactions = block.transactions();
  const auto& smartSignatures = block.smartSignatures();

  if (smartSignatures.empty()) {
    if (containsNewState(transactions)) {
        cserror() << kLogPrefix << "no smart signatures in block "
                  << block.sequence() << ", which contains new state";
        return ErrorType::error;
    }
    return ErrorType::noError;
  }

  bool switchCountedFees = block.version() == kBlockVerToSwitchCountedFees;
  auto smartPacks = grepNewStatesPacks(transactions, switchCountedFees);

  if (!checkSignatures(smartSignatures, smartPacks)) {
    return ErrorType::error;
  }

  return ErrorType::noError;
}

bool SmartSourceSignaturesValidator::checkSignatures(const SmartSignatures& sigs,
                                                     const Packets& smartPacks) {
  if (sigs.size() != smartPacks.size()) {
    cserror() << kLogPrefix << "q-ty of smart signatures != q-ty of real smart packets"; 
    return false;
  }

  for (const auto& pack : smartPacks) {
    auto it = std::find_if(sigs.begin(), sigs.end(),
                           [&pack] (const csdb::Pool::SmartSignature& s) {
                           return pack.transactions()[0].source().public_key() == s.smartKey; });

    if (it == sigs.end()) {
      cserror() << kLogPrefix << "no smart signatures for new state with key "
                << pack.transactions()[0].source().to_string();
      return false;
    }

    auto initPool = getBlockChain().loadBlock(it->smartConsensusPool);
    const auto& confidants = initPool.confidants();
    const auto& smartSignatures = it->signatures;
    for (const auto& s : smartSignatures) {
      if (s.first >= confidants.size()) {
        cserror() << kLogPrefix << "smart signature validation: no conf with index "
                  << s.first << " in init pool with sequence " << initPool.sequence();
        return false;
      }
      if (!cscrypto::verifySignature(s.second, confidants[s.first], pack.hash().toBinary().data(), cscrypto::kHashSize)) {
        cserror() << kLogPrefix << "incorrect signature of smart "
                  << pack.transactions()[0].source().to_string() << " of confidant " << s.first
                  << " from init pool with sequence " << initPool.sequence();
        return false;
      }
    }
  }

  return true;
}

inline bool SmartSourceSignaturesValidator::containsNewState(const Transactions& trxs) {
  for (const auto& t : trxs) {
    if (SmartContracts::is_new_state(t)) {
      return true;
    }
  }
  return false;
}

Packets SmartSourceSignaturesValidator::grepNewStatesPacks(const Transactions& trxs, bool switchFees) {
  Packets res;
  for (size_t i = 0; i < trxs.size(); ++i) {
    if (SmartContracts::is_new_state(trxs[i])) {
      cs::TransactionsPacket pack;
      pack.addTransaction(switchFees ? switchCountedFee(trxs[i]) : trxs[i]);
      std::for_each(trxs.begin() + i + 1, trxs.end(),
          [&] (const csdb::Transaction& t) {
            if (t.source() == trxs[i].source()) {
              pack.addTransaction(switchFees ? switchCountedFee(t) : t);
            }
          });
      pack.makeHash();
      res.push_back(pack);
    }
  }
  return res;
}

csdb::Transaction SmartSourceSignaturesValidator::switchCountedFee(const csdb::Transaction& t) {
  auto initTrx = WalletsCache::findSmartContractInitTrx(t, getBlockChain());
  if (!initTrx.is_valid()) {
    cserror() << kLogPrefix << " no init transaction for smart source transaction in blockchain";
    return t;
  }
  csdb::Transaction res(t.innerID(), t.source(), t.target(), t.currency(), t.amount(), t.max_fee(),
                        initTrx.counted_fee(), t.signature());
  auto ufIds = t.user_field_ids();
  for (const auto& id : ufIds) {
    res.add_user_field(id, t.user_field(id));
  }
  return res;
}

ValidationPlugin::ErrorType BalanceChecker::validateBlock(const csdb::Pool&) {
  const auto& prevBlock = getPrevBlock();
  if (prevBlock.transactions().empty()) {
    return ErrorType::noError;
  }

  const auto& trxs = prevBlock.transactions();
  auto wallets = getWallets();
  wallets->updateFromSource();
  for (const auto& t : trxs) {
    WalletsState::WalletId id{};
    const WalletsState::WalletData& wallState = wallets->getData(t.source(), id);
    if (wallState.balance_ < zeroBalance_) {
      cserror() << kLogPrefix << "error detected in pool " << prevBlock.sequence()
                << ", wall address " << t.source().to_string()
                << " has balance " << wallState.balance_.to_double();
      return ErrorType::error;
    }
  }

  return ErrorType::noError;
}

ValidationPlugin::ErrorType TransactionsChecker::validateBlock(const csdb::Pool& block) {
  const auto& trxs = block.transactions();
  std::set<csdb::Address> newStates;
  for (const auto& t : trxs) {
    if (SmartContracts::is_new_state(t)) {
      // already checked by another plugin
      newStates.insert(t.source());
      continue;
    }

    auto it = std::find(newStates.begin(), newStates.end(), t.source());
    if (it != newStates.end()) {
      continue;
    }

    if (!checkSignature(t)) {
      cserror() << kLogPrefix << " in pool " << block.sequence()
                << " transaction from " << t.source().to_string()
                << ", with innerID " << t.innerID()
                << " has incorrect signature";
      return ErrorType::error;
    }
  }
  return ErrorType::noError;
}

bool TransactionsChecker::checkSignature(const csdb::Transaction& t) {
  if (t.source().is_wallet_id()) {
    const auto& bc = getBlockChain();
    BlockChain::WalletData dataToFetchPublicKey;
    if (!bc.findWalletData(t.source().wallet_id(), dataToFetchPublicKey)) {
      cserror() << kLogPrefix << "no public key for id "
                << t.source().wallet_id() << " in blockchain";
      return false;
    }
    return t.verify_signature(dataToFetchPublicKey.address_);
  } else {
    return t.verify_signature(t.source().public_key());
  }
}

} // namespace cs
