#include "solvercontext.hpp"
#include "solvercore.hpp"
#include "smartcontracts.hpp"

#include <csnode/conveyer.hpp>
#include <csnode/node.hpp>
#include <lib/system/logger.hpp>

namespace cs {

  /*static*/
  cs::Hash SolverContext::zeroHash;

  /*static*/
  cs::Signature SolverContext::zeroSignature;

  /*static*/
  cs::PublicKey SolverContext::zeroKey;

BlockChain& SolverContext::blockchain() const {
  return core.pnode->getBlockChain();
}

std::string SolverContext::sender_description(const cs::PublicKey & sender_id)
{
  // either RVO or string's move constructor used:
  return core.pnode->getSenderText(sender_id);
}

void SolverContext::add_stage1(cs::StageOne& stage, bool send) {
  // core.stageOneStorage.push_back(stage);
  if (send) {
    core.pnode->sendStageOne(stage);
  }
  /*the order is important! the signature is created in node
  before sending stage and then is inserted in the field .sig
  now we can add it to stages storage*/
  core.gotStageOne(stage);
}

void SolverContext::add_stage2(cs::StageTwo& stage, bool send) {
  // core.stageTwoStorage.push_back(stage);

  if (send) {
    core.pnode->sendStageTwo(stage);
  }
  /*the order is important! the signature is created in node
  before sending stage and then is inserted in the field .sig
  now we can add it to stages storage*/
  core.gotStageTwo(stage);
}

void SolverContext::add_stage3(cs::StageThree& stage) {
  // core.stageThreeStorage.push_back(stage);

  core.pnode->sendStageThree(stage);
  /*the order is important! the signature is created in node
  before sending stage and then is inserted in the field .sig
  now we can add it to stages storage*/
  core.gotStageThree(stage, 1);
}

size_t SolverContext::own_conf_number() const {
  return (size_t)core.pnode->getConfidantNumber();
}

size_t SolverContext::cnt_trusted() const {
  return cs::Conveyer::instance().confidantsCount();
}

const std::vector<cs::PublicKey>& SolverContext::trusted() const {
  return cs::Conveyer::instance().confidants();
}

void SolverContext::request_round_table() const {
  //        core.pnode->sendRoundTableRequest(core.cur_round);
}

Role SolverContext::role() const {
  auto v = core.pnode->getNodeLevel();
  switch (v) {
    case NodeLevel::Normal:
    case NodeLevel::Main:
      return Role::Normal;
    case NodeLevel::Confidant:
    case NodeLevel::Writer:
      return Role::Trusted;
    default:
      break;
  }
  cserror() << "SolverCore: unknown NodeLevel value " << static_cast<int>(v) << " was returned by Node";
  // TODO: how to handle "unknown" node level value?
  return Role::Normal;
}

void SolverContext::spawn_next_round(cs::StageThree& st3) {
  std::string tStamp;
  uint8_t writer_idx = InvalidConfidantIndex;
  const auto own_stage3 = stage3((uint8_t) own_conf_number());
  //if(own_stage3 != nullptr) {
  //  writer_idx = own_stage3->writer;
  //  csdebug() << "writer idx = " << (int)writer_idx ;
  //}
  //else {
  //csdebug() << "else" ;
  //  for(const auto& s3 : stage3_data()) {
  //    writer_idx = s3.writer;
  //    if(writer_idx != InvalidConfidantIndex) {
  //      break;
  //    }
  //  }
  //}
  //if(writer_idx != InvalidConfidantIndex) {
  //  csdebug() << "writer_idx != invalid";
  //  auto ptr = stage1(writer_idx);
  //  if(ptr != nullptr) {
  //    csdebug() << "ptr(my stage 1) != nullptr";
  //    tStamp = ptr->roundTimeStamp;
  //  }
  //}


  if (st3.sender != InvalidConfidantIndex) {
    writer_idx = st3.writer;
    csdebug() << "writer idx = " << (int)writer_idx;
  }
  else {
    cserror() << "Writer wans't elected on this node";
    return;
  }
  if (writer_idx != InvalidConfidantIndex) {
    csdebug() << "writer_idx != invalid";
    auto ptr = stage1(writer_idx);
    if (ptr != nullptr) {
      csdebug() << "ptr(my stage 1) != nullptr";
      tStamp = ptr->roundTimeStamp;
    }
  }

  if(tStamp.empty()) {
    cswarning() << "SolverCore: cannot act as writer because lack writer timestamp";
    return;
  }

  csdebug() << "SolverCore: spawn next round";
  if (core.trusted_candidates.empty()) {
    cserror() << "SolverCore: trusted candidates list must not be empty while spawn next round";
  }
  csdebug() << "SolverCore: new confidant nodes: ";
  int i = 0;
  for (auto& it : core.trusted_candidates) {
    csdebug() << '\t' << i << ". " << cs::Utils::byteStreamToHex(it.data(), it.size());
    ++i;
  }
  csdebug() << "SolverCore: new hashes count is " << core.hashes_candidates.size();
  core.spawn_next_round(core.trusted_candidates, core.hashes_candidates, std::move(tStamp), st3);
}

void SolverContext::sendRoundTable() {
  core.sendRoundTable();
}

csdb::Address SolverContext::optimize(const csdb::Address& address) const {
  csdb::internal::WalletId id;
  if (core.pnode->getBlockChain().findWalletId(address, id)) {
    return csdb::Address::from_wallet_id(id);
  }
  return address;
}

bool SolverContext::test_trusted_idx(uint8_t idx, const cs::PublicKey& sender) {
  // vector<Hash> confidantNodes_ in Node actually stores PublicKey items :-)
  const auto& trusted = this->trusted();
  if (idx < trusted.size()) {
    const auto& pk = *(trusted.cbegin() + idx);
    return 0 == memcmp(pk.data(), sender.data(), pk.size());
  }
  return false;
}

cs::Bytes SolverContext::last_block_hash() const {
  // if(!core.is_block_deferred()) {
  return core.pnode->getBlockChain().getLastHash().to_binary();
  //}
  // return core.deferred_block.hash().to_binary().data();
}

void SolverContext::request_stage1(uint8_t from, uint8_t required) {
  const auto& conveyer = cs::Conveyer::instance();
  if (!conveyer.isConfidantExists(from)) {
    return;
  }
  csdebug() << "SolverCore: ask [" << (int)from << "] for stage-1 of [" << (int)required << "]";
  core.pnode->stageRequest(MsgTypes::FirstStageRequest, from, required);
}

void SolverContext::request_stage2(uint8_t from, uint8_t required) {
  const auto& conveyer = cs::Conveyer::instance();
  if (!conveyer.isConfidantExists(from)) {
    return;
  }
  csdebug() << "SolverCore: ask [" << (int)from << "] for stage-2 of [" << (int)required << "]";
  core.pnode->stageRequest(MsgTypes::SecondStageRequest, from, required);
}

void SolverContext::request_stage3(uint8_t from, uint8_t required) {
  const auto& conveyer = cs::Conveyer::instance();
  if (!conveyer.isConfidantExists(from)) {
    return;
  }
  csdebug() << "SolverCore: ask [" << (int)from << "] for stage-3 of [" << (int)required << "]";
  core.pnode->stageRequest(MsgTypes::ThirdStageRequest, from, required);
}

bool SolverContext::transaction_still_in_pool(int64_t inner_id) const {
  auto lock = cs::Conveyer::instance().lock();

  const auto& block = cs::Conveyer::instance().transactionsBlock();
  for (const auto& packet : block) {
    for (const auto& tr : packet.transactions()) {
      if (tr.innerID() == inner_id) {
        return true;
      }
    }
  }
  return false;
}

void SolverContext::request_round_info(uint8_t respondent1, uint8_t respondent2) {
  csdebug() << "SolverCore: ask [" << (int)respondent1 << "] for RoundTable";
  core.pnode->sendRoundTableRequest(respondent1);
  csdebug() << "SolverCore: ask [" << (int)respondent2 << "] for RoundTable";
  core.pnode->sendRoundTableRequest(respondent2);
}

}  // namespace slv2
