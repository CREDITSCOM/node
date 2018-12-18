#include <solvercontext.hpp>
#include <states/trustedstage3state.hpp>

#include <csnode/datastream.hpp>

#include <cmath>
#include <csnode/blockchain.hpp>
#include <lib/system/utils.hpp>

namespace cs {
void TrustedStage3State::on(SolverContext& context) {
  DefaultStateBehavior::on(context);

  stage.realTrustedMask.clear();
  stage.sender = (uint8_t)context.own_conf_number();
  const auto ptr = context.stage2(stage.sender);
  if (ptr == nullptr) {
    if (Consensus::Log) {
      LOG_WARN(name() << ": stage one result not found");
    }
  }
  // process already received stage-2, possible to go further to stage-3
  if (!context.stage2_data().empty()) {
    cslog() << name() << ": handle early received stages-2";
    for (const auto& st : context.stage2_data()) {
      csdebug() << name() << ": stage-2[" << (int) st.sender << "]";
      if (Result::Finish == onStage2(context, st)) {
        context.complete_stage3();
        return;
      }
    }
  }

  // 3 subsequent timeouts:
  //  - request stages-2 from origins
  //  - request stages-2 from anyone
  //  - create fake stages-2 from outbound nodes and force to next state

  SolverContext* pctx = &context;
  if (Consensus::Log) {
    LOG_NOTICE(name() << ": start track timeout " << Consensus::T_stage_request << " ms of stages-2 received");
  }
  timeout_request_stage.start(
      context.scheduler(), Consensus::T_stage_request,
      // timeout #1 handler:
      [pctx, this]() {
        if (Consensus::Log) {
          LOG_NOTICE(name() << ": timeout for stages-2 is expired, make requests");
        }
        request_stages(*pctx);
        // start subsequent track timeout for "wide" request
        if (Consensus::Log) {
          LOG_NOTICE(name() << ": start subsequent track timeout " << Consensus::T_stage_request
                            << " ms to request neighbors about stages-2");
        }
        timeout_request_neighbors.start(
            pctx->scheduler(), Consensus::T_stage_request,
            // timeout #2 handler:
            [pctx, this]() {
              if (Consensus::Log) {
                LOG_NOTICE(name() << ": timeout for transition is expired, make requests to neighbors");
              }
              request_stages_neighbors(*pctx);
              // timeout #3 handler
              timeout_force_transition.start(
                pctx->scheduler(), Consensus::T_stage_request,
                [pctx, this]() {
                  cslog() << name() << ": timeout for transition is expired, mark silent nodes as outbound";
                  mark_outbound_nodes(*pctx);
                },
                true/*replace if exists*/);
        },
            true /*replace if exists*/);
      },
      true /*replace if exists*/);
}

void TrustedStage3State::off(SolverContext& /*context*/) {
  if (timeout_request_stage.cancel()) {
    if (Consensus::Log) {
      LOG_NOTICE(name() << ": cancel track timeout of stages-2");
    }
  }
  if (timeout_request_neighbors.cancel()) {
    if (Consensus::Log) {
      LOG_NOTICE(name() << ": cancel track timeout to request neighbors about stages-2");
    }
  }
  if(timeout_force_transition.cancel()) {
    cslog() << name() << ": cancel track timeout to force transition to next state";
  }
}

// requests stages from corresponded nodes
void TrustedStage3State::request_stages(SolverContext& context) {
  uint8_t cnt = (uint8_t)context.cnt_trusted();
  int cnt_requested = 0;
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage2(i) == nullptr) {
      context.request_stage2(i, i);
      ++cnt_requested;
    }
  }
  if(0 == cnt_requested) {
    csdebug() << name() << ": no node ot request";
  }
}

// requests stages from any available neighbor nodes
void TrustedStage3State::request_stages_neighbors(SolverContext& context) {
  const auto& stage2_data = context.stage2_data();
  uint8_t cnt = (uint8_t)context.cnt_trusted();
  int cnt_requested = 0;
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage2(i) == nullptr) {
      for (const auto& d : stage2_data) {
        if (d.sender != context.own_conf_number()) {
          context.request_stage2(d.sender, i);
          ++cnt_requested;
        }
      }
    }
  }
  if(0 == cnt_requested) {
    csdebug() << name() << ": no node ot request";
  }
}

// forces transition to next stage
void TrustedStage3State::mark_outbound_nodes(SolverContext& context)
{
  uint8_t cnt = (uint8_t) context.cnt_trusted();
  for(uint8_t i = 0; i < cnt; ++i) {
    if(context.stage2(i) == nullptr) {
      // it is possible to get a transition to other state in SolverCore from any iteration, this is not a problem, simply execute method until end
      context.fake_stage2(i);
    }
  }
}

Result TrustedStage3State::onStage2(SolverContext& context, const cs::StageTwo&) {
  const auto ptr = context.stage2((uint8_t)context.own_conf_number());
  if (ptr != nullptr && context.enough_stage2()) {
    cslog() << name() << ": enough stage-2 received";
    const size_t cnt = context.cnt_trusted();
    for (auto& it : context.stage2_data()) {
      if ( it.sender != context.own_conf_number()) {
        for (size_t j = 0; j < cnt; j++) {
          // check amount of trusted node's signatures nonconformity
          if (ptr->signatures[j] != it.signatures[j]) {
            if(it.hashes[j] == SolverContext::zeroHash) {
              cslog() << name() << ": [" << (int) it.sender << "] marked as untrusted (silent)";
              context.mark_untrusted(it.sender);
              continue;
            }
            cs::Bytes toVerify;
            size_t messageSize = sizeof(cs::RoundNumber) + sizeof(cs::Hash);
            toVerify.reserve(messageSize);
            cs::DataStream stream(toVerify);
            stream << (uint32_t)context.round(); // Attention!!! the uint32_t type
            stream << it.hashes[j];

            if (cscrypto::VerifySignature(it.signatures[j], context.trusted().at(it.sender), toVerify.data(), messageSize)) {
              cslog() << name() << ": [" << (int)j << "] marked as untrusted";
              context.mark_untrusted((uint8_t)j);
            }
            else {
              cslog() << name() << ": [" << (int)it.sender << "] marked as untrusted";
              context.mark_untrusted(it.sender);
            }

          }
        }

        bool toBreak = false;
        size_t tCandSize = context.stage1(it.sender)->trustedCandidates.size();
        if(tCandSize > 0) {
          for(size_t outer = 0; outer < tCandSize - 1; outer++) {
            for(size_t inner = outer + 1; inner < tCandSize; inner++) {
              if(context.stage1(it.sender)->trustedCandidates.at(outer) == context.stage1(it.sender)->trustedCandidates.at(inner)) {
                cslog() << name() << ": [" << (int) it.sender << "] marked as untrusted";
                context.mark_untrusted(it.sender);
                toBreak = true;
                break;
              }
            }
            if(toBreak) {
              break;
            }
          }
        }
        else {
          cslog() << name() << ": [" << (int) it.sender << "] marked as untrusted (no candidates)";
          context.mark_untrusted(it.sender);
        }
      }
    }

    trusted_election(context);

    cswarning() << "============================ CONSENSUS SUMMARY =================================";
    if (pool_solution_analysis(context)) {
      stage.writer = take_urgent_decision(context); //to be redesigned
      cswarning() << "\t==> [" << (int)stage.writer << "]";
    }
    else {
      cswarning() << "\tconsensus is not achieved";
      /*the action is needed*/
    }
    cswarning() << "================================================================================";

    // all trusted nodes must send stage3 data
    LOG_NOTICE(name() << ": --> stage-3 [" << (int)stage.sender << "]");
    context.add_stage3(stage);  //, stage.writer != stage.sender);
    context.next_trusted_candidates(next_round_trust, next_round_hashes);
    return Result::Finish;
  }
  LOG_DEBUG(name() << ": continue to receive stages-2");
  return Result::Ignore;
}

bool TrustedStage3State::pool_solution_analysis(SolverContext& context) {
  struct HashWeight {
    cs::Hash hash;
    uint8_t weight{0};
  };

  std::vector<HashWeight> hWeight;
  HashWeight everyHashWeight;
  // creating hash frequency table
  for (const auto& it : context.stage1_data()) {
    if (hWeight.size() == 0) {
      std::copy(it.hash.cbegin(), it.hash.cend(), everyHashWeight.hash.begin());
      everyHashWeight.weight = 1;
      hWeight.push_back(everyHashWeight);
    }
    else {
      bool found = false;
      for (auto& itt : hWeight) {
        if (itt.hash == it.hash) {
          ++(itt.weight);
          found = true;
          break;
        }
      }
      if (!found) {
        std::copy(it.hash.cbegin(), it.hash.cend(), everyHashWeight.hash.begin());
        everyHashWeight.weight = 1;
        hWeight.push_back(everyHashWeight);
      }
    }
  }
  size_t maxWeight = 0;
  cs::Hash mostFrequentHash;
  std::fill(mostFrequentHash.begin(), mostFrequentHash.end(), (cs::Byte)0);

  ////searching for most frequent hash
  for (auto& it : hWeight) {
    if (it.weight > maxWeight) {
      maxWeight = it.weight;
      std::copy(it.hash.cbegin(), it.hash.cend(), mostFrequentHash.begin());
    }
  }
  uint8_t liarNumber = 0;
  /* cslog() <<  "Most Frequent hash: " << byteStreamToHex((const char*)mostFrequentHash.val, cscrypto::kHashSize);*/
  for (const auto& it : context.stage1_data()) {
    if (std::equal(it.hash.cbegin(), it.hash.cend(), mostFrequentHash.cbegin())) {
      cslog() << "[" << (int)it.sender << "] is not liar";
    }
    else {
      ++liarNumber;
      bool is_lost = (std::equal(it.hash.cbegin(), it.hash.cend(), SolverContext::zeroHash.cbegin()));
      cslog() << "[" << (int)it.sender << "] IS " << (is_lost ? "LOST" : "LIAR") <<" with hash "
              << cs::Utils::byteStreamToHex(it.hash.data(), it.hash.size());
    }
  }
  // TODO: modify to select right confidants to trusted_mask 
  if (liarNumber > 0) {
    cswarning() << "\tLiars detected: " << (int)liarNumber;
  }
  else {
    cswarning() << "\tNo liars detected";
  }
  if (liarNumber > context.cnt_trusted() / 2) {
    return false;
  }
  else {
    return true;
  }
}

void TrustedStage3State::trusted_election(SolverContext& context) {
  if (!next_round_trust.empty()) {
    next_round_trust.clear();
  }
  if (!next_round_hashes.empty()) {
    next_round_hashes.clear();
  }
  std::array<uint8_t, Consensus::MaxTrustedNodes> trustedMask;
  trustedMask.fill(0);
  std::map<cs::PublicKey, uint8_t> candidatesElection;
  size_t myPacks = 0;
  std::vector < cs::TransactionsPacketHash> myHashes;
  std::map <cs::TransactionsPacketHash, uint8_t> hashesElection;
  std::vector < cs::TransactionsPacketHash> myRejectedHashes;
  const uint8_t cnt_trusted = std::min((uint8_t)context.cnt_trusted(), (uint8_t)Consensus::MaxTrustedNodes);
  uint8_t cr = cnt_trusted / 2;
  std::vector<cs::PublicKey> aboveThreshold;
  std::vector<cs::PublicKey> belowThreshold;
  cslog() << name() << ": number of generals / 2 = " << (int)cr;

  for (uint8_t i = 0; i < cnt_trusted; i++) {
    trustedMask[i] = (context.untrusted_value(i) == 0);
    if (trustedMask[i]) {
      stage.realTrustedMask.push_back(1); // set 1 if trusted and 0 if untrusted
      auto ptr = context.stage1(i);
      if(ptr == nullptr) {
        continue;
      }
      const auto& stage_i = *ptr;
      uint8_t candidates_amount = (uint8_t) stage_i.trustedCandidates.size();
      cslog() << "Candidates amount of [" << (int)i << "] : " << (int)candidates_amount;


      for (uint8_t j = 0; j < candidates_amount; j++) {
      //  cslog() << (int)i << "." << (int)j << " " << cs::Utils::byteStreamToHex(stage_i.trustedCandidates.at(j).data(), cscrypto::kPublicKeySize);
        if (candidatesElection.count(stage_i.trustedCandidates.at(j)) > 0) {
          candidatesElection.at(stage_i.trustedCandidates.at(j)) += 1;
        }
        else {
          candidatesElection.emplace(stage_i.trustedCandidates.at(j), (uint8_t)1);
        }
      }
      size_t hashes_amount = stage_i.hashesCandidates.size();
      //cslog() << "My conf number = " << context.own_conf_number();
      if (stage_i.sender == (uint8_t)context.own_conf_number()) {
        myHashes = stage_i.hashesCandidates;
        myPacks = stage_i.hashesCandidates.size();
      }


      cslog() << "Hashes amount of [" << (int)i << "]: " << (int)hashes_amount;
      for (uint32_t j = 0; j < hashes_amount; j++) {
         // cslog() << (int)i << "." << j << " " << cs::Utils::byteStreamToHex(stage_i.hashesCandidates.at(j).toBinary().data(), cscrypto::kHashSize);
        if (hashesElection.count(stage_i.hashesCandidates.at(j)) > 0) {
          hashesElection.at(stage_i.hashesCandidates.at(j)) += 1;
        }
        else {
          hashesElection.emplace(stage_i.hashesCandidates.at(j), (uint8_t)1u);
        }
      }
    }
    else {
      stage.realTrustedMask.push_back(0);
    }
  }

  LOG_NOTICE(name() << ": election table ready");
  unsigned int max_conf = int(4. + 1.85 * log(candidatesElection.size() / 4.));
  if (candidatesElection.size() < 4) {
    max_conf = (unsigned int)candidatesElection.size();
    LOG_WARN(name() << ": too few TRUSTED NODES, but we continue at the minimum ...");
  }

  for (auto& it : candidatesElection) {
    // cslog() << byteStreamToHex(it.first.str, cscrypto::kPublicKeySize) << " - " << (int) it.second;
    if (it.second > cr) {
      aboveThreshold.push_back(it.first);
    }
    else {
      belowThreshold.push_back(it.first);
    }
  }

  //LOG_NOTICE(name() << ": HASHES election table ready (" << hashesElection.size() << "):");
  for (auto& it : hashesElection) {
  //  cslog() << cs::Utils::byteStreamToHex(it.first.toBinary().data(), cscrypto::kHashSize) << " - " << (int)it.second;
    if (it.second > cr) {
      next_round_hashes.push_back(it.first);
    }
  }
  size_t acceptedPacks = 0;
  bool rejectedFound;
  //cslog() << "Accepted hashes from THIS NODE: ";
  for (auto& itt : myHashes) {
    rejectedFound = true;
    //cslog() << "    " << cs::Utils::byteStreamToHex(it.toBinary().data(), it.size());
    for (auto& it : next_round_hashes) {
      if (memcmp(it.toBinary().data(), itt.toBinary().data(), cscrypto::kHashSize)) {}
      else {
        ++acceptedPacks;
        rejectedFound = false;
      //  cslog() << "    + (" << acceptedPacks << ") " << cs::Utils::byteStreamToHex(itt.toBinary().data(), itt.size());
      }
    }
    if (rejectedFound) {
      myRejectedHashes.push_back(itt);
    }
  }

  cslog() << name() << ": initial amount: " << myPacks << ", next round hashes: " << next_round_hashes.size() << ", accepted: " << acceptedPacks;
  cslog() << name() << ": candidates divided: above = " << aboveThreshold.size() << ", below = " << belowThreshold.size();
  csdebug() << "======================================================";
  for (size_t i = 0; i < aboveThreshold.size(); i++) {
    const auto& tmp = aboveThreshold.at(i);
    csdebug() << i << ". " << cs::Utils::byteStreamToHex(tmp.data(), tmp.size()) << " - " << (int)candidatesElection.at(tmp);
  }
  csdebug() << "------------------------------------------------------";
  for (size_t i = 0; i < belowThreshold.size(); i++) {
    const auto& tmp = belowThreshold.at(i);
    csdebug() << i << ". " << cs::Utils::byteStreamToHex(tmp.data(), tmp.size()) << " - " << (int)candidatesElection.at(tmp);
  }
  cslog() << name() << ": final list of next round trusted:";

  if (aboveThreshold.size() >= max_conf) {  // Consensus::MinTrustedNodes) {
    for (unsigned int i = 0; i < max_conf; ++i) {
      const auto& tmp = aboveThreshold.at(i);
      next_round_trust.push_back(tmp);
      cslog() << "\t" << cs::Utils::byteStreamToHex(tmp.data(), tmp.size());
    }
  }
  else {
    if (belowThreshold.size() >= max_conf - aboveThreshold.size()) {
      for (size_t i = 0; i < aboveThreshold.size(); i++) {
        const auto& tmp = aboveThreshold.at(i);
        next_round_trust.push_back(tmp);
        LOG_NOTICE(cs::Utils::byteStreamToHex(tmp.data(), tmp.size()));
      }
      for (size_t i = 0; i < max_conf - next_round_trust.size(); i++) {
        const auto& tmp = belowThreshold.at(i);
        next_round_trust.push_back(tmp);
        LOG_NOTICE(cs::Utils::byteStreamToHex(tmp.data(), tmp.size()));
      }
    }
    else {
      LOG_WARN(name() << ": cannot create list of trusted, too few candidates.");
    }
  }
  LOG_NOTICE(name() << ": end of trusted election");
}

uint8_t TrustedStage3State::take_urgent_decision(SolverContext& context) {
  auto hash_t = context.blockchain().getHashBySequence(static_cast<uint32_t>(context.round()) - 1).to_binary();
  if (hash_t.empty()) {
    return 0;  // TODO: decide what to return
  }
  int k = *(hash_t.begin());
  // cslog() << "K : " << k;
  int result0 = (int)context.cnt_trusted();
  int result = 0;
  result = k % result0;
  return (uint8_t)result;
}

}  // namespace slv2
