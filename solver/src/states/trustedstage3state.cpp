#include <solvercontext.hpp>
#include <states/trustedstage3state.hpp>

#include <csnode/datastream.hpp>
#include <csnode/blockchain.hpp>
#include <csnode/conveyer.hpp>

#include <lib/system/utils.hpp>

#include <cmath>
#include <algorithm>

namespace cs {
void TrustedStage3State::on(SolverContext& context) {
  DefaultStateBehavior::on(context);
  if(!context.realTrustedChanged()) {
    stage.iteration = 0;
    stage.realTrustedMask.clear();
    stage.realTrustedMask.resize(context.cnt_trusted());
    stage.sender = context.own_conf_number();
  }
  else {
    ++(stage.iteration);
    stage.realTrustedMask.clear();
    stage.realTrustedMask = context.getRealTrusted();// we delete this storage so the realtrusted will be zero
  }
  context.realTrustedChangedSet(false);
  cnt_recv_stages = 0;

  if (std::count(stage.realTrustedMask.cbegin(), stage.realTrustedMask.cend(), cs::ConfidantConsts::InvalidConfidantIndex) > 0) {
    if (Result::Finish == finalizeStageThree(context)) {
      context.complete_stage3();
      return;
    } 
    else {
      cswarning() << name() << "the stage can't finish successfully, waiting for Big Bang";
      context.fail_stage3();
      return;
    }
  }

  const auto ptr = context.stage2(stage.sender);
  if (ptr == nullptr) {
    cswarning() << name() << ": stage one result not found";
  }
  // process already received stage-2, possible to go further to stage-3
  if (!context.stage2_data().empty()) {
    csdebug() << name() << ": handle early received stages-2";
    Result finish = Result::Ignore;
    for (const auto& st : context.stage2_data()) {
      csdebug() << name() << ": stage-2[" << static_cast<int>(st.sender) << "] has already received";
      if (Result::Finish == onStage2(context, st)) {
        finish = Result::Finish;
      }
    }

    if (finish == Result::Finish) {
      context.complete_stage3();
      return;
    }

    if (finish == Result::Failure) {
      context.fail_stage3();
      return;
    }
  }

  // 3 subsequent timeouts:
  //  - request stages-2 from origins
  //  - request stages-2 from anyone
  //  - create fake stages-2 from outbound nodes and force to next state

  SolverContext* pctx = &context;
  auto dt = 2 * Consensus::T_stage_request;
  csdebug() << name() << ": start track timeout " << 0 << " ms of stages-2 received";
  timeout_request_stage.start(
      context.scheduler(), 0, // no timeout
      // timeout #1 handler:
      [pctx, this, dt]() {
       csdebug() << name() << ": (now) skip direct requests for absent stages-2";
        //request_stages(*pctx);
        // start subsequent track timeout for "wide" request
        csdebug() << name() << ": start subsequent track timeout " << dt << " ms to request neighbors about stages-2";
        timeout_request_neighbors.start(
            pctx->scheduler(), dt,
            // timeout #2 handler:
            [pctx, this, dt]() {
              csdebug() << name() << ": timeout for transition is expired, make requests to neighbors";
              request_stages_neighbors(*pctx);
              cs::RoundNumber rnum = cs::Conveyer::instance().currentRoundNumber();
              // timeout #3 handler
              csdebug() << name() << ": start subsequent track timeout " << dt << " ms to mark silent nodes";
              timeout_force_transition.start(
                pctx->scheduler(), dt,
                [pctx, this, rnum, dt]() {
                  csdebug() << name() << ": timeout for transition is expired, mark silent nodes as outbound";
                  mark_outbound_nodes(*pctx, rnum);
                },
                true/*replace if exists*/);
            },
            true /*replace if exists*/);
      },
      true /*replace if exists*/);
}

void TrustedStage3State::off(SolverContext& /*context*/) {
  if (timeout_request_stage.cancel()) {
    csdebug() << name() << ": cancel track timeout of stages-2";
  }
  if (timeout_request_neighbors.cancel()) {
    csdebug() << name() << ": cancel track timeout to request neighbors about stages-2";
  }
  if (timeout_force_transition.cancel()) {
    csdebug() << name() << ": cancel track timeout to force transition to next state";
  }
}

// requests stages from corresponded nodes
void TrustedStage3State::request_stages(SolverContext& context) {
  auto cnt = static_cast<uint8_t>(context.cnt_trusted());
  int cnt_requested = 0;

  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage2(i) == nullptr) {
      context.request_stage2(i, i);
      ++cnt_requested;
    }
  }

  if (cnt_requested == 0) {
    csdebug() << name() << ": no node to request";
  }
}

// requests stages from any available neighbor nodes
void TrustedStage3State::request_stages_neighbors(SolverContext& context) {
  const auto& stage2_data = context.stage2_data();
  auto cnt = static_cast<uint8_t>(context.cnt_trusted());
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

  if (cnt_requested == 0) {
    csdebug() << name() << ": no node to request";
  }
}

// forces transition to next stage
void TrustedStage3State::mark_outbound_nodes(SolverContext& context, cs::RoundNumber round) {
  csdebug() << name() << ": mark outbound nodes in round #" << round;
  auto cnt = static_cast<uint8_t>(context.cnt_trusted());
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage2(i) == nullptr) {
      // it is possible to get a transition to other state in SolverCore from any iteration, this is not a problem, simply execute method until end
      csdebug() << name() << ": making fake stage-2 in round " << round;
      context.fake_stage2(i);
      // this procedute can cause the round change
      if (round != cs::Conveyer::instance().currentRoundNumber()) {
        return;
      }
    }
  }
}

Result TrustedStage3State::onStage2(SolverContext& context, const cs::StageTwo&) {
  const auto ptr = context.stage2(context.own_conf_number());
  ++cnt_recv_stages;
  if (ptr != nullptr && cnt_recv_stages == context.cnt_trusted()) {
    csdebug() << name() << ": enough stage-2 received";
    const size_t cnt = context.cnt_trusted();
    for (auto& it : context.stage2_data()) {
      if ( it.sender != context.own_conf_number()) {
        csdebug() << "Comparing with T(" << static_cast<int>(ptr->sender) << "):";
        for (size_t j = 0; j < cnt; j++) {
          // check amount of trusted node's signatures nonconformity
          csdetails() << "Signature of T(" << j << ") in my storage is: " << cs::Utils::byteStreamToHex(ptr->signatures[j]);
          if (ptr->signatures[j] != it.signatures[j]) {
            csdebug() << "Signature of T(" << j << ") sent by T(" << static_cast<int>(it.sender) << "):" << cs::Utils::byteStreamToHex(it.signatures[j])
                    << " from stage-2 is not equal to mine";

            if (it.hashes[j] == SolverContext::zeroHash) {
              csdebug() << name() << ": [" << static_cast<int>(it.sender) << "] marked as untrusted (silent)";
              context.mark_untrusted(it.sender);
              continue;
            }

            cs::Bytes toVerify;
            size_t messageSize = sizeof(cs::RoundNumber) + sizeof(uint8_t) + sizeof(cs::Hash);
            toVerify.reserve(messageSize);
            cs::DataStream stream(toVerify);
            stream << cs::Conveyer::instance().currentRoundNumber() << context.subRound(); // Attention!!! the uint32_t type
            stream << it.hashes[j];

            if (cscrypto::verifySignature(it.signatures[j], context.trusted().at(it.sender), toVerify.data(), messageSize)) {
              cslog() << name() << ": [" << static_cast<int>(j) << "] marked as untrusted (sent bad hash-signature pair of [" << static_cast<int>(it.sender) << "])";
              context.mark_untrusted(static_cast<uint8_t>(j));
            }
            else {
              cslog() << name() << ": [" << static_cast<int>(it.sender) << "] marked as untrusted (bad signature)";
              context.mark_untrusted(it.sender);
            }

          }
        }

        bool toBreak = false;
        size_t tCandSize = 0;
        const auto ptrStage1 = context.stage1(it.sender);
        if (ptrStage1 != nullptr) {
          tCandSize = ptrStage1->trustedCandidates.size();
        }

        if (tCandSize > 0) {
          for (size_t outer = 0; outer < tCandSize - 1; outer++) {
//DPOS check start -> comment if unnecessary
            if (!context.checkNodeCache(ptrStage1->trustedCandidates.at(outer))) {
              cslog() << name() << ": [" << static_cast<int>(it.sender) << "] marked as untrusted (low-value candidates)";
              context.mark_untrusted(it.sender);
              break;
            }
//DPOS check finish
            for (size_t inner = outer + 1; inner < tCandSize; inner++) {
              if (ptrStage1->trustedCandidates.at(outer) == ptrStage1->trustedCandidates.at(inner)) {
                cslog() << name() << ": [" << static_cast<int>(it.sender) << "] marked as untrusted (duplicated candidates)";
                context.mark_untrusted(it.sender);
                toBreak = true;
                break;
              }
            }
            if (toBreak) {
              break;
            }
          }
        }
        else {
          cslog() << name() << ": [" << static_cast<int>(it.sender) << "] marked as untrusted (no candidates)";
          context.mark_untrusted(it.sender);
        }
      }
    }

    trusted_election(context);

    csdebug() << "============================ CONSENSUS SUMMARY =================================";
    if (pool_solution_analysis(context)) {
      if (take_urgent_decision(context)) {  // to be redesigned
        csdebug() << "\t==> [" << static_cast<int>(stage.writer) << "]";
      }
      else {
        cslog() << "\tconsensus failed waiting for BigBang";
        return Result::Failure;
      }
    }
    else {
      cslog() << "\tconsensus is not achieved";
      return Result::Failure;
      /*the action is needed*/
    }
    csdebug() << "================================================================================";

    // all trusted nodes must send stage3 data
    context.next_trusted_candidates(next_round_trust, next_round_hashes);
    //TODO: The pool building is starting here <===
    context.spawn_next_round(stage);
    csdebug() << name() << ": --> stage-3 [" << static_cast<int>(stage.sender) << "]";
    context.add_stage3(stage);  //, stage.writer != stage.sender);

    return Result::Finish;
  }

  csdebug() << name() << ": continue to receive stages-2";
  return Result::Ignore;
}

Result TrustedStage3State::finalizeStageThree(SolverContext& context) {
//TODO : here write the code to clean stage three storage, perhaps add st3 iterations
  if (take_urgent_decision(context)) {  // to be redesigned
    csdebug() << "\t==> [" << static_cast<int>(stage.writer) << "]";
  }
  else {
    cslog() << "\tconsensus failed: waiting for BigBang";
    return Result::Failure;
  }
  csdebug() << "Starting new collection of stage 3 because a part of nodes didn't respond correct";
  context.spawn_next_round(stage);
  csdebug() << name() << ": --> stage-3 [" << static_cast<int>(stage.sender) << "]";
  context.add_stage3(stage);  //, stage.writer != stage.sender);

  return Result::Finish;
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
  mostFrequentHash.fill(0);

  ////searching for most frequent hash
  for (auto& it : hWeight) {
    if (it.weight > maxWeight) {
      maxWeight = it.weight;
      std::copy(it.hash.cbegin(), it.hash.cend(), mostFrequentHash.begin());
    }
  }
  uint8_t liarNumber = 0;
  /* csdebug() <<  "Most Frequent hash: " << byteStreamToHex((const char*)mostFrequentHash.val, cscrypto::kHashSize);*/
  for (const auto& it : context.stage1_data()) {
    if (it.sender >= stage.realTrustedMask.size()) {
      cserror() << name() << ": index of sender is greater than the container size";
      return false;
    }
    if (std::equal(it.hash.cbegin(), it.hash.cend(), mostFrequentHash.cbegin()) && stage.realTrustedMask.at(it.sender)!= cs::ConfidantConsts::InvalidConfidantIndex) {
      csdebug() << "[" << static_cast<int>(it.sender) << "] is not liar";
    }
    else {
      ++liarNumber;
      context.mark_untrusted(it.sender);
      stage.realTrustedMask.at(it.sender) = cs::ConfidantConsts::InvalidConfidantIndex;

      bool is_lost = (std::equal(it.hash.cbegin(), it.hash.cend(), SolverContext::zeroHash.cbegin()));
      csdebug() << "[" << static_cast<int>(it.sender) << "] IS " << ((is_lost && stage.realTrustedMask.at(it.sender) == cs::ConfidantConsts::InvalidConfidantIndex) ? "LOST" : "LIAR") <<" with hash "
              << cs::Utils::byteStreamToHex(it.hash);
    }
  }

  // TODO: modify to select right confidants to trusted_mask 
  if (liarNumber > 0) {
    cswarning() << "\tLiars detected: " << static_cast<int>(liarNumber);
  }
  else {
    csdebug() << "\tNo liars detected";
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
  const uint8_t cnt_trusted = std::min(static_cast<uint8_t>(context.cnt_trusted()), static_cast<uint8_t>(Consensus::MaxTrustedNodes));
  uint8_t cr = cnt_trusted / 2;
  std::vector<cs::PublicKey> aboveThreshold;
  std::vector<cs::PublicKey> belowThreshold;
  csdebug() << name() << ": number of generals / 2 = " << static_cast<int>(cr);

  for (uint8_t i = 0; i < cnt_trusted; i++) {
    trustedMask[i] = (context.untrusted_value(i) == 0);
    if (trustedMask[i]) {
      stage.realTrustedMask.at(i) = cs::ConfidantConsts::FirstWriterIndex;
      auto ptr = context.stage1(i);
      if (ptr == nullptr) {
        continue;
      }
      const auto& stage_i = *ptr;
      uint8_t candidates_amount = static_cast<uint8_t>(stage_i.trustedCandidates.size());
      csdebug() << "Candidates amount of [" << static_cast<int>(i) << "] : " << static_cast<int>(candidates_amount);


      for (uint8_t j = 0; j < candidates_amount; j++) {
      //  csdebug() << (int)i << "." << (int)j << " " << cs::Utils::byteStreamToHex(stage_i.trustedCandidates.at(j).data(), cscrypto::kPublicKeySize);
        if (candidatesElection.count(stage_i.trustedCandidates.at(j)) > 0) {
          candidatesElection.at(stage_i.trustedCandidates.at(j)) += 1;
        }
        else {
          candidatesElection.emplace(stage_i.trustedCandidates.at(j), uint8_t(1));
        }
      }

      size_t hashes_amount = stage_i.hashesCandidates.size();
      //csdebug() << "My conf number = " << context.own_conf_number();
      if (stage_i.sender == context.own_conf_number()) {
        myHashes = stage_i.hashesCandidates;
        myPacks = stage_i.hashesCandidates.size();
      }

      csdebug() << "Hashes amount of [" << static_cast<int>(i) << "]: " << static_cast<int>(hashes_amount);
      for (uint32_t j = 0; j < hashes_amount; j++) {
         // csdebug() << (int)i << "." << j << " " << cs::Utils::byteStreamToHex(stage_i.hashesCandidates.at(j).toBinary().data(), cscrypto::kHashSize);
        if (hashesElection.count(stage_i.hashesCandidates.at(j)) > 0) {
          hashesElection.at(stage_i.hashesCandidates.at(j)) += 1;
        }
        else {
          hashesElection.emplace(stage_i.hashesCandidates.at(j), uint8_t(1));
        }
      }
    }
    else {
      stage.realTrustedMask.at(i) = cs::ConfidantConsts::InvalidConfidantIndex;
    }
  }

  csdebug() << name() << ": election table ready";
  size_t max_conf = 0;
  if (candidatesElection.size() < 4) {
    max_conf = candidatesElection.size();
    csdebug() << name() << ": too few TRUSTED NODES, but we continue at the minimum ...";
  }
  else {
    max_conf = static_cast<size_t>(4. + 1.85 * log(candidatesElection.size() / 4.));
  }
  csdebug() << name() << ": max confidant: " << max_conf;

  for (auto& it : candidatesElection) {
    // csdebug() << byteStreamToHex(it.first.str, cscrypto::kPublicKeySize) << " - " << (int) it.second;
    if (it.second > cr) {
      aboveThreshold.emplace_back(it.first);
    }
    else {
      belowThreshold.emplace_back(it.first);
    }
  }

  for (auto& it : hashesElection) {
    if (it.second > cr) {
      next_round_hashes.emplace_back(it.first);
    }
  }
  size_t acceptedPacks = 0;
  for (const auto& hash : myHashes) {
    bool rejectedFound = true;
    for (const auto& next_hash : next_round_hashes) {
      if (next_hash != hash) {
        ++acceptedPacks;
        rejectedFound = false;
      }
    }
    if (rejectedFound) {
      myRejectedHashes.emplace_back(hash);
    }
  }

  csdebug() << name() << ": initial amount: " << myPacks << ", next round hashes: " << next_round_hashes.size() << ", accepted: " << acceptedPacks;
  csdebug() << name() << ": candidates divided: above = " << aboveThreshold.size() << ", below = " << belowThreshold.size();
  csdebug() << "======================================================";

  for (size_t i = 0; i < aboveThreshold.size(); i++) {
    const auto& tmp = aboveThreshold[i];
    csdebug() << i << ". " << cs::Utils::byteStreamToHex(tmp.data(), tmp.size())
              << " - " << static_cast<int>(candidatesElection.at(tmp));
  }

  csdebug() << "------------------------------------------------------";
  for (size_t i = 0; i < belowThreshold.size(); i++) {
    const auto& tmp = belowThreshold[i];
    csdebug() << i << ". " << cs::Utils::byteStreamToHex(tmp.data(), tmp.size())
              << " - " << static_cast<int>(candidatesElection.at(tmp));
  }

  csdebug() << name() << ": final list of next round trusted:";

  if (aboveThreshold.size() >= max_conf) {  // Consensus::MinTrustedNodes) {
    std::random_device rd;
    std::mt19937 g;
    g.seed( (unsigned int) Conveyer::instance().currentRoundNumber());
    cs::shuffle(aboveThreshold.begin(), aboveThreshold.end(), g);
    for (size_t i = 0; i < max_conf; ++i) {
      const auto& tmp = aboveThreshold.at(i);
      next_round_trust.emplace_back(tmp);
      csdebug() << "\t" << cs::Utils::byteStreamToHex(tmp.data(), tmp.size());
    }
  }
  else {
    if (belowThreshold.size() >= max_conf - aboveThreshold.size()) {
      for (size_t i = 0; i < aboveThreshold.size(); i++) {
        const auto& tmp = aboveThreshold.at(i);
        next_round_trust.emplace_back(tmp);
        csdebug() << cs::Utils::byteStreamToHex(tmp.data(), tmp.size());
      }
      const size_t toAdd = max_conf - next_round_trust.size();
      for (size_t i = 0; i < toAdd; i++) {
        const auto& tmp = belowThreshold.at(i);
        next_round_trust.emplace_back(tmp);
        csdebug() << cs::Utils::byteStreamToHex(tmp.data(), tmp.size());
      }
    }
    else {
      cslog() << name() << ": cannot create list of trusted, too few candidates.";
    }
  }
  csdebug() << name() << ": end of trusted election";
}

bool TrustedStage3State::take_urgent_decision(SolverContext& context) {
  auto hash_t = context.blockchain().getHashBySequence(cs::Conveyer::instance().currentRoundNumber() - 1).to_binary();
  if (hash_t.empty()) {
    return false;  // TODO: decide what to return
  }
  int k = *(unsigned int*)hash_t.data();
  if (k < 0) {
    k = -k;
  }
  // stage.realTrustedMask contains !0 on good nodes:
  int cnt = static_cast<int>(context.cnt_trusted());
  int cnt_active = cnt - static_cast<int>(std::count(stage.realTrustedMask.cbegin(), stage.realTrustedMask.cend(), InvalidConfidantIndex));
  if (cnt_active * 2 < cnt + 1) {
    cswarning() << name() << ": not enough active confidants to make a decision, BigBang required";
    return false;
  }
  int idx_writer = k % cnt_active;
  if (cnt != cnt_active) {
    csdebug() << "\tselect #" << idx_writer << " from " << cnt_active << " good nodes in " << cnt << " total";
  }
  else {
    csdebug() << "\tselect #" << idx_writer << " from " << cnt << " nodes";
  }
  // count idx_writer through good nodes (optional):
  int idx = 0;
  for (size_t i = 0; i < size_t(cnt); ++i) {
    if (stage.realTrustedMask.at(i) != InvalidConfidantIndex) {
      if (idx == idx_writer) {
        stage.writer = static_cast<uint8_t>(i);
      }
      ++idx;
    }
  }
  size_t c = 0;
  idx = 0;
  for (size_t i = stage.writer; i < size_t(cnt + stage.writer); ++i) {
    c = i % size_t(cnt);
    if (stage.realTrustedMask.at(c) != InvalidConfidantIndex) {
      stage.realTrustedMask.at(c) = static_cast<uint8_t>(idx);
      ++idx;
    }
  }
  if (std::count(stage.realTrustedMask.cbegin(), stage.realTrustedMask.cend(), cs::ConfidantConsts::InvalidConfidantIndex) > stage.realTrustedMask.size() / 2U + 1U) {
    return false;
  }
  return true;
}

}  // namespace slv2
