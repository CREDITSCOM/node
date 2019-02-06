#include <consensus.hpp>
#include <solvercontext.hpp>
#include <states/trustedstage1state.hpp>
#include <smartcontracts.hpp>

#include <csnode/blockchain.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/transactionspacket.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <cscrypto/cscrypto.hpp>

namespace cs {
void TrustedStage1State::on(SolverContext& context) {
  if (!ptransval) {
    ptransval = std::make_unique<cs::TransactionsValidator>(context.wallets(), cs::TransactionsValidator::Config{});
  }

  DefaultStateBehavior::on(context);
  context.init_zero(stage);
  stage.sender = context.own_conf_number();

  enough_hashes = false;
  transactions_checked = false;
}

void TrustedStage1State::off(SolverContext& context) {
  csdebug() << name() << ": --> stage-1 [" << static_cast<int>(stage.sender) << "]";
  context.add_stage1(stage, true);
}

Result TrustedStage1State::onSyncTransactions(SolverContext& context, cs::RoundNumber round) {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  if (round < conveyer.currentRoundNumber()) {
    cserror() << name() << ": cannot handle previous round transactions";
    return Result::Ignore;
  }

  csdebug() << name() << ": -------> STARTING CONSENSUS #" << conveyer.currentRoundNumber() << " <------- ";
  auto data = conveyer.createPacket();

  if (!data.has_value()) {
    cserror() << name() << ": error while prepare consensus to build vector, maybe method called before sync completed?";
    return Result::Ignore;
  }

  // bindings
  auto&& [packet, smartContractPackets] = std::move(data).value();

  csdebug() << name() << ": packet of " << packet.transactionsCount() << " transactions in" << typeid(conveyer).name();
  csdebug() << name() << ": smart contract packets size " << smartContractPackets.size();

  // TODO: do something with smartContractPackets

  // review & validate transactions
  context.blockchain().setTransactionsFees(packet);
  stage.hash = build_vector(context, packet);

  {
    std::unique_lock<cs::SharedMutex> lock = conveyer.lock();
    const cs::RoundTable& roundTable = conveyer.currentRoundTable();

    for (const auto& element : conveyer.transactionsPacketTable()) {
      const cs::PacketsHashes& hashes = roundTable.hashes;

      if (std::find(hashes.cbegin(), hashes.cend(), element.first) == hashes.cend()) {
        stage.hashesCandidates.push_back(element.first);

        if (stage.hashesCandidates.size() > Consensus::maxStageOneHashes) {
          break;
        }
      }
    }
  }

  transactions_checked = true;

  return (enough_hashes ? Result::Finish : Result::Ignore);
}

Result TrustedStage1State::onHash(SolverContext& context, const csdb::PoolHash& pool_hash,
                                  const cs::PublicKey& sender) {
  
  csdb::PoolHash lastHash = context.blockchain().getLastHash();
  csdb::PoolHash spoiledHash = context.spoileHash(lastHash, sender);
  csdebug() << name() << ": <-- hash from " << context.sender_description(sender);
  if (spoiledHash == pool_hash) {
    // get node status for useful logging

    if (stage.trustedCandidates.size() <= Consensus::MaxTrustedNodes) {
      csdebug() << name() << ": hash is OK";
      if (std::find(stage.trustedCandidates.cbegin(), stage.trustedCandidates.cend(), sender) == stage.trustedCandidates.cend()) {
        stage.trustedCandidates.push_back(sender);
      }
    }
    if (stage.trustedCandidates.size() >= Consensus::MinTrustedNodes)  {
      // enough hashes
      // flush deferred block to blockchain if any
      enough_hashes = true;
      return (transactions_checked ? Result::Finish : Result::Ignore);
    }
  }
  else {
    cslog() << name() << ": DOES NOT MATCH to my value " << lastHash.to_string();
    context.sendHashReply(std::move(pool_hash), sender);
  }


  return Result::Ignore;
}

// removes "bad" transactions from p:
void TrustedStage1State::filter_test_signatures(SolverContext& context, cs::TransactionsPacket& p) {
  auto& vec = p.transactions();
  if (vec.empty()) {
    return;
  }
  const BlockChain& bc = context.blockchain();
  auto cnt_filtered = 0;
  for (auto it = vec.begin(); it != vec.end(); ++it) {
    const auto& src = it->source();
    bool verifyResult = false;

    if (src.is_wallet_id()) {
      BlockChain::WalletData data_to_fetch_pulic_key;
      bc.findWalletData(src.wallet_id(), data_to_fetch_pulic_key);
      verifyResult = it->verify_signature(data_to_fetch_pulic_key.address_);
    }
    else {
      verifyResult = it->verify_signature(src.public_key());
    }

    if (!verifyResult) {
      it = vec.erase(it);
      ++cnt_filtered;
      if (it == vec.end()) {
        break;
      }
    }
  }

  if (cnt_filtered > 0) {
    cswarning() << name() << ": " << cnt_filtered << " trans. filtered while test signatures";
  }
}

cs::Hash TrustedStage1State::build_vector(SolverContext& context, const cs::TransactionsPacket& packet) {
  const std::size_t transactionsCount = packet.transactionsCount();
  const auto& transactions = packet.transactions();

  cs::Characteristic characteristic;

  if (transactionsCount > 0) {
    context.wallets().updateFromSource();
    ptransval->reset(transactionsCount);

    cs::Bytes characteristicMask;
    characteristicMask.reserve(transactionsCount);

    uint8_t del1;

    for (std::size_t i = 0; i < transactionsCount; ++i) {
      const auto& smarts = context.smart_contracts();
      const csdb::Transaction& transaction = transactions[i];
      bool byte = true;
      bool is_smart_new_state = smarts.is_new_state(transaction);
      if (!is_smart_new_state) {
        byte = !(transaction.source() == transaction.target());
        byte = byte && ptransval->validateTransaction(transaction, i, del1);
      }
      else {
        //TODO: implement appropriate validation of smart-state transactions
        csdebug() << name() << ": smart new_state trx[" << i << "] included in consensus";
        if (context.smart_contracts().is_closed_smart_contract(transaction.target())) {
          byte = false;
          cslog() << name() << ": reject smart new_state trx because related contract is closed";
        }
      }

      if (!byte) {
        cslog() << name() << ": trx[" << i << "] rejected in validateTransaction()";
      }

      if (byte) {
        // yrtimd: test with get_valid_smart_address() only for deploy transactions:
        if (smarts.is_deploy(transaction)) {
          auto sci = context.smart_contracts().get_smart_contract(transaction);
          if (sci.has_value() && sci.value().method.empty()) {  // Is deploy
            csdb::Address deployer = context.blockchain().get_addr_by_type(transaction.source(), BlockChain::ADDR_TYPE::PUBLIC_KEY); 
            byte = static_cast<cs::Byte>(SmartContracts::get_valid_smart_address(deployer, transaction.innerID(), sci.value().smartContractDeploy) == transaction.target());

            if (!byte) {
              cslog() << name() << ": trx[" << i << "] rejected due to incorrect smart address";
            }
          }
        }

        if (byte) {
          byte = static_cast<cs::Byte>(check_transaction_signature(context, transaction));
          if (!byte) {
            cslog() << name() << ": trx[" << i << "] rejected by check_transaction_signature()";
          }
        }
      }
      else {
        cslog() << name() << ": trx[" << i << "] rejected by validateTransaction()";
      }

      characteristicMask.push_back(byte ? cs::Byte(1) : cs::Byte(0));
    }

    csdb::Pool excluded;
    ptransval->validateByGraph(characteristicMask, packet.transactions(), excluded);
    if (excluded.transactions_count() > 0) {
      cslog() << name() << ": " << excluded.transactions_count() << " transactions are rejected in validateByGraph()";
    }

    // test if smart-emitted transaction rejected, reject all transactions from this smart
    // 1. collect rejected smart addresses
    const auto& smarts = context.smart_contracts();
    std::set<csdb::Address> smart_rejected;
    size_t mask_size = characteristicMask.size();
    size_t i = 0;
    for (const auto& tr : transactions) {
      if (i < mask_size && *(characteristicMask.cbegin() + i) == 0) {
        if (smarts.is_known_smart_contract(tr.source())) {
          smart_rejected.insert(tr.source());
        }
      }
      ++i;
    }
    if (!smart_rejected.empty()) {
      cslog() << name() << ": detected rejected trxs from " << smart_rejected.size() << " smart contract(s)";
      cs::TransactionsPacket rejected;

      // 2. reject all trxs from those smarts & collect all rejected trxs
      size_t cnt_add_rejected = 0;
      for (auto it = transactions.begin(); it != transactions.end(); ++it) {
        if (smart_rejected.count(it->source()) > 0) {
          auto itm = characteristicMask.begin() + (it - transactions.cbegin());
          if (*itm > 0) {
            *itm = 0;
            ++cnt_add_rejected;
          }
          rejected.addTransaction(*it);
        }
      }
      if (cnt_add_rejected > 0) {
        cslog() << name() << ": additionaly rejected " << cnt_add_rejected << " trxs";
      }

      // 3. signal SmartContracts service some trxs are rejected
      if (rejected.transactionsCount() > 0) {
        context.smart_contracts().on_reject(rejected);
      }
    }

    characteristic.mask = std::move(characteristicMask);
  }

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  conveyer.setCharacteristic(characteristic, conveyer.currentRoundNumber());

  if (characteristic.mask.size() != transactionsCount) {
    cserror() << name() << ": characteristic mask size is not equal to transactions count in build_vector()";
  }

  cs::Hash hash;

  if (characteristic.mask.empty()) {
    auto round = conveyer.currentRoundNumber();
    hash = cscrypto::calculateHash(reinterpret_cast<cs::Byte*>(&round), sizeof(cs::RoundNumber));
  }
  else {
    hash = cscrypto::calculateHash(characteristic.mask.data(), characteristic.mask.size());
  }

  csdebug() << name() << ": generated hash: " << cs::Utils::byteStreamToHex(hash.data(), hash.size());
  return hash;
}

bool TrustedStage1State::check_transaction_signature(SolverContext& context, const csdb::Transaction& transaction) {
  BlockChain::WalletData data_to_fetch_pulic_key;
  csdb::Address src = transaction.source();
  // TODO: is_known_smart_contract() does not recognize not yet deployed contract, so all transactions emitted in constructor
  // currently will be rejected
  if (!context.smart_contracts().is_smart_contract(transaction)) {
    if (src.is_wallet_id()) {
      context.blockchain().findWalletData(src.wallet_id(), data_to_fetch_pulic_key);
      return transaction.verify_signature(data_to_fetch_pulic_key.address_);
    }

    return transaction.verify_signature(src.public_key());
  }
  else {
    if (context.smart_contracts().is_new_state(transaction)) {
      // special rule for new_state transactions
      if (src != transaction.target()) {
        csdebug() << name() << ": smart state trx has different source and target";
        return false;
      }
      return true;
    }
    // TODO: add here code for validating signatures in the smart contract transaction
    return true;
  }
}

}  // namespace slv2
