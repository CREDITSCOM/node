#include "waitingstate.hpp"
#include <consensus.hpp>
#include <solvercontext.hpp>
#include <sstream>

namespace cs {

void WaitingState::on(SolverContext &context) {
  // TODO:: calculate my queue number starting from writing node:
  const auto ptr = context.stage3((uint8_t)context.own_conf_number());
  writingQueueNumber_ = ptr->realTrustedMask.at(ptr->sender);  
  if (writingQueueNumber_ == InvalidConfidantIndex) {
    return;
  }
  std::vector<std::pair<uint8_t, cscrypto::Signature>> blockSignatures;
  for (auto& it : context.stage3_data()) {
    blockSignatures.emplace_back(it.sender,it.blockSignature);
  }
  csdebug() << "Signatures to add: " << blockSignatures.size();
  if (context.addSignaturesToLastBlock(blockSignatures)) {
    csdebug() << "Signatures added to new block successfully";
  }
  std::ostringstream os;
  os << prefix_ << "-" << (size_t)writingQueueNumber_;
  myName_ = os.str();

  size_t cnt_active = ptr->realTrustedMask.size() - std::count(ptr->realTrustedMask.cbegin(), ptr->realTrustedMask.cend(), InvalidConfidantIndex);
  csdebug() << name() << ": my order " << (int)ptr->sender << ", trusted " << (int)context.cnt_trusted() << " (good " << cnt_active << ")"
          << ", writer " << (int)ptr->writer;
  if (writingQueueNumber_ == 0) {
    csdebug() << name() << ": becoming WRITER";
    context.request_role(Role::Writer);
    return;
  }

  // TODO: value = (Consensus::PostConsensusTimeout * "own number in "writing" queue")
  uint32_t value = sendRoundTableDelayMs_ * writingQueueNumber_;

  if (Consensus::Log) {
    csdebug() << name() << ": start wait " << value / 1000 << " sec until new round";
  }

  SolverContext *pctx = &context;
  roundTimeout_.start(context.scheduler(), value,
                      [pctx, this]() {
                        if (Consensus::Log) {
                          csdebug() << name() << ": time to wait new round is expired";
                        }
                        activate_new_round(*pctx);
                      },
                      true /*replace existing*/);
}

void WaitingState::off(SolverContext & /*context*/) {
  myName_ = prefix_;
  if (roundTimeout_.cancel()) {
    if (Consensus::Log) {
      csdebug() << name() << ": cancel wait new round";
    }
  }
}

void WaitingState::activate_new_round(SolverContext &context) {
  csdebug() << name() << ": activating new round ";
  const auto ptr = context.stage3((uint8_t)context.own_conf_number());
  if (ptr == nullptr) {
    cserror() << name() << ": cannot access own stage data, didnt you forget to cancel this call?";
    return;
  }

  context.request_round_info(
      (uint8_t)((int)(ptr->writer + writingQueueNumber_ - 1)) % (int)context.cnt_trusted(),   // previous "writer"
      (uint8_t)((int)(ptr->writer + writingQueueNumber_ + 1)) % (int)context.cnt_trusted());  // next "writer"
}

}  // namespace cs
