#include <solvercontext.hpp>
#include <states/trustedstage3state.hpp>

#include <math.h>
#include <csnode/blockchain.hpp>
#include <lib/system/utils.hpp>

namespace slv2 {
void TrustedStage3State::on(SolverContext& context) {
  DefaultStateBehavior::on(context);

  memset(&stage, 0, sizeof(stage));
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
      if (Result::Finish == onStage2(context, st)) {
        context.complete_stage3();
        return;
      }
    }
  }

  SolverContext* pctx = &context;
  if (Consensus::Log) {
    LOG_NOTICE(name() << ": start track timeout " << Consensus::T_stage_request << " ms of stages-2 received");
  }
  timeout_request_stage.start(
      context.scheduler(), Consensus::T_stage_request,
      // timeout handler:
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
            // timeout handler:
            [pctx, this]() {
              if (Consensus::Log) {
                LOG_NOTICE(name() << ": timeout for transition is expired, make requests to neighbors");
              }
              request_stages_neighbors(*pctx);
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
}

// requests stages from corresponded nodes
void TrustedStage3State::request_stages(SolverContext& context) {
  uint8_t cnt = (uint8_t)context.cnt_trusted();
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage2(i) == nullptr) {
      context.request_stage2(i, i);
    }
  }
}

// requests stages from any available neighbor nodes
void TrustedStage3State::request_stages_neighbors(SolverContext& context) {
  const auto& stage2_data = context.stage2_data();
  uint8_t cnt = (uint8_t)context.cnt_trusted();
  for (uint8_t i = 0; i < cnt; ++i) {
    if (context.stage2(i) == nullptr) {
      for (const auto& d : stage2_data) {
        if (d.sender != context.own_conf_number()) {
          context.request_stage2(d.sender, i);
        }
      }
    }
  }
}

Result TrustedStage3State::onStage2(SolverContext& context, const cs::StageTwo& st) {
  const auto ptr = context.stage2((uint8_t)context.own_conf_number());
  if (ptr != nullptr && context.enough_stage2()) {
    LOG_NOTICE(name() << ": enough stage-2 received");
    const size_t cnt = context.cnt_trusted();
    constexpr size_t sig_len = sizeof(st.signatures[0].size());
    for (size_t i = 0; i < cnt; i++) {
      // check amount of trusted node's signatures nonconformity
      // TODO: redesign required:
      if (memcmp(ptr->signatures[i].data(), st.signatures[i].data(), sig_len) != 0) {
        LOG_WARN(name() << ": [" << (int)st.sender << "] marked as untrusted");
        context.mark_untrusted(st.sender);
      }
    }

    trusted_election(context);

    cswarning() << "============================ CONSENSUS SUMMARY =================================";
    if (pool_solution_analysis(context)) {
      stage.writer = take_urgent_decision(context);
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
    context.next_trusted_candidates(next_round_trust);
    // if(stage.writer == stage.sender) {
    //    // we are selected to write & send block
    //    LOG_NOTICE(name() << ": spawn next round");
    //    context.spawn_next_round();
    //}
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
  /* cslog() <<  "Most Frequent hash: " << byteStreamToHex((const char*)mostFrequentHash.val, 32);*/
  for (const auto& it : context.stage1_data()) {
    if (std::equal(it.hash.cbegin(), it.hash.cend(), mostFrequentHash.cbegin())) {
      cslog() << "[" << (int)it.sender << "] is not liar";
    }
    else {
      ++liarNumber;
      cslog() << "[" << (int)it.sender << "] IS LIAR with hash "
              << cs::Utils::byteStreamToHex(it.hash.data(), it.hash.size());
    }
  }

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
  std::array<uint8_t, Consensus::MaxTrustedNodes> trustedMask;
  trustedMask.fill(0);
  std::map<cs::PublicKey, uint8_t> candidatesElection;
  const uint8_t cnt_trusted = std::min((uint8_t)context.cnt_trusted(), (uint8_t)Consensus::MaxTrustedNodes);
  uint8_t cr = cnt_trusted / 2;
  std::vector<cs::PublicKey> aboveThreshold;
  std::vector<cs::PublicKey> belowThreshold;

  LOG_NOTICE(name() << ": number of generals / 2 = " << (int)cr);

  for (uint8_t i = 0; i < cnt_trusted; i++) {
    trustedMask[i] = (context.untrusted_value(i) <= cr);
    if (trustedMask[i]) {
      const auto& stage_i = *(context.stage1_data().cbegin() + i);
      uint8_t candidates_amount = stage_i.candidatesAmount;
      for (uint8_t j = 0; j < candidates_amount; j++) {
        // cslog() << (int) i << "." << (int) j << " " << byteStreamToHex(stageOneStorage.at(i).candiates[j].str, 32);
        if (candidatesElection.count(stage_i.candiates[j]) > 0) {
          candidatesElection.at(stage_i.candiates[j]) += 1;
        }
        else {
          candidatesElection.emplace(stage_i.candiates[j], (uint8_t)1);
        }
      }
    }
  }

  LOG_NOTICE(name() << ": election table ready");
  unsigned int max_conf = int(4. + 1.85 * log(candidatesElection.size() / 4.));
  if (candidatesElection.size() < 4) {
    max_conf = (unsigned int)candidatesElection.size();
    LOG_WARN(name() << ": too few TRUSTED NODES, but we continue at the minimum ...");
  }

  for (auto& it : candidatesElection) {
    // cslog() << byteStreamToHex(it.first.str, 32) << " - " << (int) it.second;
    if (it.second > cr) {
      aboveThreshold.push_back(it.first);
    }
    else {
      belowThreshold.push_back(it.first);
    }
  }

  LOG_NOTICE(name() << ": candidates divided: above = " << aboveThreshold.size()
                    << ", below = " << belowThreshold.size());
  LOG_DEBUG("======================================================");
  for (size_t i = 0; i < aboveThreshold.size(); i++) {
    const auto& tmp = aboveThreshold.at(i);
    LOG_DEBUG(i << ". " << cs::Utils::byteStreamToHex(tmp.data(), tmp.size()) << " - "
                << (int)candidatesElection.at(tmp));
  }
  LOG_DEBUG("------------------------------------------------------");
  for (size_t i = 0; i < belowThreshold.size(); i++) {
    const auto& tmp = belowThreshold.at(i);
    LOG_DEBUG(i << ". " << cs::Utils::byteStreamToHex(tmp.data(), tmp.size()) << " - "
                << (int)candidatesElection.at(tmp));
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
