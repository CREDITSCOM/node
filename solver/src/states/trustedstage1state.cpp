#include <consensus.hpp>
#include <solvercontext.hpp>
#include <states/trustedstage1state.hpp>

#include <csnode/blockchain.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/transactionspacket.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <blake2.h>
#include <sstream>

namespace slv2 {
void TrustedStage1State::on(SolverContext& context) {
  if (!ptransval) {
    ptransval = std::make_unique<cs::TransactionsValidator>(context.wallets(), cs::TransactionsValidator::Config{});
  }

  DefaultStateBehavior::on(context);

  // if we were Writer un the previous round, we have a deferred block, flush it:
  if (context.is_block_deferred()) {
    context.flush_deferred_block();
  }

  memset(&stage, 0, sizeof(stage));
  stage.sender = (uint8_t)context.own_conf_number();
  enough_hashes = false;
  transactions_checked = false;
}

void TrustedStage1State::off(SolverContext& context) {
  cslog() << name() << ": --> stage-1 [" << (int)stage.sender << "]";
  context.add_stage1(stage, true);
}

void TrustedStage1State::onRoundEnd(SolverContext& context, bool is_bigbang) {
  // in this stage we got round end only having troubles
  if (context.is_block_deferred()) {
    if (is_bigbang) {
      context.drop_deferred_block();
    }
    else {
      context.flush_deferred_block();
    }
  }
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
#if LOG_LEVEL & FLAG_LOG_DEBUG
  std::ostringstream os;
  for (const auto& t : p.transactions()) {
    os << " " << t.innerID();
  }
        csdebug() << name() << ":" << os.str());
#endif  // FLAG_LOG_DEBUG

        // obsolete?
        // pool = filter_test_signatures(context, pool);

        // see Solver::runCinsensus()
        context.blockchain().setTransactionsFees(pack);
        stage.hash = build_vector(context, pack);
        transactions_checked = true;

        return (enough_hashes ? Result::Finish : Result::Ignore);
}

Result TrustedStage1State::onHash(SolverContext& context, const csdb::PoolHash& pool_hash,
                                  const cs::PublicKey& sender) {
  // get node status for useful logging
  std::string sender_status("N");
  unsigned idx = 0;
  for (const auto& key : context.trusted()) {
    if (std::equal(key.cbegin(), key.cend(), sender.cbegin())) {
      std::ostringstream os;
      os << "T[" << idx << "]";
      sender_status = os.str();
      break;
    }
    ++idx;
  }

  cslog() << name() << ": <-- hash from " << sender_status << " ("
          << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << ")";
  const auto& lwh = context.blockchain().getLastWrittenHash();
  if (stage.candidatesAmount < Consensus::MinTrustedNodes) {
    if (pool_hash == lwh) {
      cslog() << name() << ": hash is OK";

      bool keyFound = false;
      for (uint8_t i = 0; i < stage.candidatesAmount; i++) {
        if (stage.candiates[i] == sender) {
          keyFound = true;
          break;
        }
      }
      if (!keyFound) {
        stage.candiates[stage.candidatesAmount] = sender;
        stage.candidatesAmount += 1;
      }
    }
    else {
      // hash does not match to own hash
      cswarning() << name() << ": hash " << pool_hash.to_string() << " from "
                  << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " DOESN'T match to my value "
                  << lwh.to_string();
      return Result::Ignore;
    }
  }
  if (stage.candidatesAmount >= Consensus::MinTrustedNodes) {
    // enough hashes
    // flush deferred block to blockchain if any
    if (context.is_block_deferred()) {
      context.flush_deferred_block();
    }
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
      const csdb::Transaction& transaction = transactions[i];
      cs::Byte byte = static_cast<cs::Byte>(ptransval->validateTransaction(transaction, i, del1));

      if (byte) {
        byte = static_cast<cs::Byte>(check_transaction_signature(context, transaction));
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

  conveyer.setCharacteristic(characteristic, static_cast<cs::RoundNumber>(context.round()));

  if (characteristic.mask.size() != transactionsCount) {
    cserror() << "Trusted-1: characteristic mask size not equals transactions count in build_vector()";
  }

  cs::Hash hash;
  blake2s(hash.data(), hash.size(), characteristic.mask.data(), characteristic.mask.size(), nullptr, 0u);
  csdebug() << "Trusted-1: Generated hash: " << cs::Utils::byteStreamToHex(hash.data(), hash.size());

  return hash;
}

bool TrustedStage1State::check_transaction_signature(SolverContext& context, const csdb::Transaction& transaction) {
  BlockChain::WalletData data_to_fetch_pulic_key;

  if (transaction.source().is_wallet_id()) {
    context.blockchain().findWalletData(transaction.source().wallet_id(), data_to_fetch_pulic_key);

    csdb::internal::byte_array byte_array(data_to_fetch_pulic_key.address_.begin(),
                                          data_to_fetch_pulic_key.address_.end());
    return transaction.verify_signature(byte_array);
  }

  return transaction.verify_signature(transaction.source().public_key());
}

}  // namespace slv2
