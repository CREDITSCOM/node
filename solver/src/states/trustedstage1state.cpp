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

  cs::Utils::clearMemory(stage);
  stage.sender = static_cast<uint8_t>(context.own_conf_number());
  enough_hashes = false;
  transactions_checked = false;
}

void TrustedStage1State::off(SolverContext& context) {
  cslog() << name() << ": --> stage-1 [" << (int)stage.sender << "]";
  context.add_stage1(stage, true);
}

Result TrustedStage1State::onSyncTransactions(SolverContext& context, cs::RoundNumber round) {
  if (round < context.round()) {
    cserror() << name() << ": cannot handle previous round transactions";
    return Result::Ignore;
  }
  cslog() << name() << ": -------> STARTING CONSENSUS #" << context.round() << " <------- ";
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  auto maybe_pack = conveyer.createPacket();
  if (!maybe_pack.has_value()) {
    cserror() << name()
      << ": error while prepare consensus to build vector, maybe method called before sync completed?";
    return Result::Ignore;
  }
  cs::TransactionsPacket pack = std::move(maybe_pack.value());
  cslog() << name() << ": packet of " << pack.transactionsCount() << " transactions in conveyer";

  // review & validate transactions
  context.blockchain().setTransactionsFees(pack);
  stage.hash = build_vector(context, pack);

  {
    bool found = false;
    cs::SharedLock lock(conveyer.sharedMutex());
    for (const auto& element : conveyer.transactionsPacketTable()) {
      found = false;
      const auto rt = conveyer.roundTable(context.round());
      if(rt != nullptr) {
        for(const auto& it : rt->hashes) {
          if(memcmp(it.toBinary().data(), element.first.toBinary().data(), cscrypto::kHashSize) == 0) {
            found = true;
          }
        }
      }
      if (!found) stage.hashesCandidates.push_back(element.first);
    }
  }

  transactions_checked = true;

  return (enough_hashes ? Result::Finish : Result::Ignore);
}

Result TrustedStage1State::onHash(SolverContext& context, const csdb::PoolHash& pool_hash,
                                  const cs::PublicKey& sender) {
  // get node status for useful logging
  cslog() << name() << ": <-- hash from " << context.sender_description(sender);
  const auto& lwh = context.blockchain().getLastWrittenHash();
  if (stage.trustedCandidates.size() < Consensus::MinTrustedNodes) {
    if (pool_hash == lwh) {
      cslog() << name() << ": hash is OK";

      bool keyFound = false;
      for (auto& it : stage.trustedCandidates) {
        if (it == sender) {
          keyFound = true;
          break;
        }
      }
      if (!keyFound) {
        stage.trustedCandidates.push_back(sender);
      }
    }
    else {
      // hash does not match to own hash
      cswarning() << name() << ": hash " << pool_hash.to_string() << " from "
                  << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " DOES NOT MATCH to my value "
                  << lwh.to_string();
      return Result::Ignore;
    }
  }
  if (stage.trustedCandidates.size() >= Consensus::MinTrustedNodes) {
    // enough hashes
    // flush deferred block to blockchain if any
    enough_hashes = true;
    return (transactions_checked ? Result::Finish : Result::Ignore);
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
    csdb::internal::byte_array pk;
    if (src.is_wallet_id()) {
      BlockChain::WalletData data_to_fetch_pulic_key;
      bc.findWalletData(src.wallet_id(), data_to_fetch_pulic_key);
      pk.assign(data_to_fetch_pulic_key.address_.cbegin(), data_to_fetch_pulic_key.address_.cend());
    }
    else {
      const auto& tmpref = src.public_key();
      pk.assign(tmpref.cbegin(), tmpref.cend());
    }
    if (!it->verify_signature(pk)) {
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

  cs::Conveyer& conveyer = cs::Conveyer::instance();
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
      cs::Byte byte = static_cast<cs::Byte>(true);
      if(!smarts.is_new_state( transaction)) {
        byte = static_cast<cs::Byte>(ptransval->validateTransaction(transaction, i, del1));
      }
      else {
        //TODO: implement appropriate validation of smart-state transactions 
      }

      if (byte) {
        // yrtimd: test with get_valid_smart_address() only for deploy transactions:
        if (smarts.is_deploy(transaction)) {
          auto sci = context.smart_contracts().get_smart_contract(transaction);
          if (sci.has_value() && sci.value().method.empty()) {  // Is deploy
            csdb::Address deployer = context.blockchain().get_addr_by_type(transaction.source(), BlockChain::ADDR_TYPE::PUBLIC_KEY); 
            byte = static_cast<cs::Byte>(SmartContracts::get_valid_smart_address(deployer, transaction.innerID(), sci.value().smartContractDeploy) == transaction.target());

            if (!byte) {
              csdebug() << name() << ": trx[" << i << "] rejected due to incorrect smart address";
            }
          }
        }

        if (byte) {
          byte = static_cast<cs::Byte>(check_transaction_signature(context, transaction));
          if(!byte) {
            csdebug() << name() << ": trx[" << i << "] rejected by check_transaction_signature()";
          }
        }
      }
      else {
        //csdebug() 
        cslog() << name() << ": trx[" << i << "] rejected by validateTransaction()";
      }

      characteristicMask.push_back(byte);
    }

    csdb::Pool excluded;
    ptransval->validateByGraph(characteristicMask, packet.transactions(), excluded);
    if (excluded.transactions_count() > 0) {
      cslog() << name() << ": " << excluded.transactions_count() << " transactions excluded in build_vector()";
    }

    characteristic.mask = std::move(characteristicMask);
  }

  conveyer.setCharacteristic(characteristic, context.round());

  if (characteristic.mask.size() != transactionsCount) {
    cserror() << "Trusted-1: characteristic mask size not equals transactions count in build_vector()";
  }

  cs::Hash hash;

  if (characteristic.mask.empty()) {
    auto round = cs::Conveyer::instance().currentRoundNumber();
    cscrypto::CalculateHash(hash, reinterpret_cast<cs::Byte*>(&round), sizeof(cs::RoundNumber));
  }
  else {
    cscrypto::CalculateHash(hash, characteristic.mask.data(), characteristic.mask.size());
  }

  csdebug() << "Trusted-1: Generated hash: " << cs::Utils::byteStreamToHex(hash.data(), hash.size());
  return hash;
}

bool TrustedStage1State::check_transaction_signature(SolverContext& context, const csdb::Transaction& transaction) {
  BlockChain::WalletData data_to_fetch_pulic_key;
  if(transaction.user_field_ids().size() != 3) {
    if (transaction.source().is_wallet_id()) {
      context.blockchain().findWalletData(transaction.source().wallet_id(), data_to_fetch_pulic_key);

      csdb::internal::byte_array byte_array(data_to_fetch_pulic_key.address_.begin(),
                                            data_to_fetch_pulic_key.address_.end());
      return transaction.verify_signature(byte_array);
    }

    return transaction.verify_signature(transaction.source().public_key());
  }
  else {
    //TODO: add here code for validating the smart contract transaction 
    return true;
  }
}

}  // namespace slv2
