#pragma once

#include "callsqueuescheduler.hpp"
#include "timeouttracking.hpp"
#include "consensus.hpp"
#include "inodestate.hpp"
#include "stage.hpp"
#include "smartconsensus.hpp"

#include <csdb/pool.hpp>
#include <csnode/transactionspacket.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

// forward declarations
class Node;

namespace cs {
class WalletsState;
class SmartContracts;
}  // namespace cs

// TODO: discuss possibility to switch states after timeout expired, timeouts can be individual but controlled by
// SolverCore

namespace cs {

  constexpr cs::Byte InvalidConfidant = 255;

class SolverCore {
public:
  using Counter = size_t;

  SolverCore();
  explicit SolverCore(Node* pNode, csdb::Address GenesisAddress, csdb::Address StartAddress);

  ~SolverCore();

  void startDefault() {
    opt_mode == Mode::Debug ? ExecuteStart(Event::SetNormal) : ExecuteStart(Event::Start);
  }

  void startAsMain() {
    ExecuteStart(Event::SetTrusted);
  }

  void finish();

  bool is_finished() const {
    return req_stop;
  }

  void sendRoundTable();

  bool addSignaturesToDeferredBlock(cs::Signatures&& blockSignatures);

  uint8_t subRound(); 
  // Solver "public" interface,
  // below are the "required" methods to be implemented by Solver-compatibility issue:

  void setKeysPair(const cs::PublicKey& pub, const cs::PrivateKey& priv);
  void gotConveyerSync(cs::RoundNumber rNum);
  void gotHash(csdb::PoolHash&& hash, const cs::PublicKey& sender);

  const cs::PublicKey& getPublicKey() const {
    return public_key;
  }

  const cs::PrivateKey& getPrivateKey() const {
    return private_key;
  }

  // TODO: requires revision
  const cs::PublicKey& getWriterPublicKey() const;

  void beforeNextRound();
  void nextRound();
  void gotRoundInfoRequest(const cs::PublicKey& requester, cs::RoundNumber requester_round);
  void gotRoundInfoReply(bool next_round_started, const cs::PublicKey& respondent);

  // Solver3 "public" extension
  void gotStageOne(const cs::StageOne& stage);
  void gotStageTwo(const cs::StageTwo& stage);
  void gotStageThree(const cs::StageThree& stage, const uint8_t flagg);

  void gotStageOneRequest(uint8_t requester, uint8_t required);
  void gotStageTwoRequest(uint8_t requester, uint8_t required);
  void gotStageThreeRequest(uint8_t requester, uint8_t required, uint8_t iteration);
  void printStage3(const cs::StageThree& stage);

  void removeDeferredBlock(cs::Sequence);
  bool realTrustedChanged() const;
  void adjustStageThreeStorage();
  void realTrustedChangedSet(bool);
  void realTrustedSetValue(cs::Byte position, cs::Byte value);
  void realTrustedSet(cs::Bytes realTrusted);
  bool checkNodeCache(const cs::PublicKey& sender);
  cs::Bytes getRealTrusted();
  size_t trueStagesThree();
  uint8_t currentStage3iteration();

  size_t stagesThree();
  bool stateFailed(Result res);


  /// <summary>   Adds a transaction passed to send pool </summary>
  ///
  /// <remarks>   Aae, 14.10.2018. </remarks>
  ///
  /// <param name="tr">   The transaction </param>

  void send_wallet_transaction(const csdb::Transaction& tr);

  cs::SmartContracts& smart_contracts() const
  {
    return *psmarts;
  }

  bool isContractLocked(const csdb::Address&) const;

private:
  // to use private data while serve for states as SolverCore context:
  friend class SolverContext;

  enum class Mode {
    Default,
    Debug,
    Monitor,
    WebWallet
  };

  enum class Event {
    Start,
    BigBang,
    RoundTable,
    Transactions,
    Hashes,
    Stage1Enough,
    Stage2Enough,
    FailConsensus,
    Stage3Enough,
    Stage3NonComplete,
    SmartDeploy,
    SmartResult,
    Expired,
    SetNormal,
    SetTrusted,
    SetWriter
  };

  using StatePtr = std::shared_ptr<INodeState>;
  using Transitions = std::map<Event, StatePtr>;

  // options

  /** @brief   True to enable, false to disable the option to track timeout of current state */
  bool opt_timeouts_enabled;

  /** @brief   True to enable, false to disable the option repeat the same state */
  bool opt_repeat_state_enabled;

  /** @brief The option mode */
  Mode opt_mode;

  // inner data

  std::unique_ptr<SolverContext> pcontext;
  CallsQueueScheduler scheduler;
  CallsQueueScheduler::CallTag tag_state_expired;
  bool req_stop;
  std::map<StatePtr, Transitions> transitions;
  StatePtr pstate;

  // consensus data

  csdb::Address addr_genesis;
  csdb::Address addr_start;
  cs::PublicKey public_key;
  cs::PrivateKey private_key;
  // senders of hashes received this round
  std::vector<std::pair<csdb::PoolHash, cs::PublicKey>> recv_hash;

  Node* pnode;
  std::unique_ptr<cs::WalletsState> pws;
  // smart contracts service
  std::unique_ptr<cs::SmartContracts> psmarts;

  void ExecuteStart(Event start_event);

  void InitTransitions();
  void InitDebugModeTransitions();
  void InitMonitorModeTransitions();
  void InitWebWalletModeTransitions();
  void setState(const StatePtr& pState);

  void handleTransitions(Event evt);
  bool stateCompleted(Result result);

  void spawn_next_round(const cs::PublicKeys& nodes,
                        const cs::PacketsHashes& hashes,
                        std::string&& currentTimeStamp,
                        cs::StageThree& st3);

  // timeout tracking

  TimeoutTracking timeout_request_stage;
  TimeoutTracking timeout_request_neighbors;
  TimeoutTracking timeout_force_transition;
  
  /**
   * @fn  cs::StageOne* SolverCore::find_stage1(uint8_t sender);
   *
   * @brief   Searches for the stage 1 of given sender
   *
   * @author  Alexander Avramenko
   * @date    07.11.2018
   *
   * @param   sender  The sender.
   *
   * @return  Null if it fails, else the found stage 1.
   */

  cs::StageOne* find_stage1(uint8_t sender) {
    return find_stage<>(stageOneStorage, sender);
  }

  /**
   * @fn  cs::StageTwo* SolverCore::find_stage2(uint8_t sender);
   *
   * @brief   Searches for the stage 2 of given sender
   *
   * @author  Alexander Avramenko
   * @date    07.11.2018
   *
   * @param   sender  The sender.
   *
   * @return  Null if it fails, else the found stage 2.
   */

  cs::StageTwo* find_stage2(uint8_t sender) {
    return find_stage<>(stageTwoStorage, sender);
  }

  /**
   * @fn  cs::StageThree* SolverCore::find_stage3(uint8_t sender);
   *
   * @brief   Searches for the stage 3 of given sender
   *
   * @author  Alexander Avramenko
   * @date    07.11.2018
   *
   * @param   sender  The sender.
   *
   * @return  Null if it fails, else the found stage 3.
   */

public:
  cs::StageThree* find_stage3(uint8_t sender) {
    return find_stage<>(stageThreeStorage, sender);
  }

private:
  const cs::StageThree* find_stage3(uint8_t sender) const {
    return find_stage<>(stageThreeStorage, sender);
  }

  template <typename StageT>
  StageT* find_stage(const std::vector<StageT>& vec, uint8_t sender) const {
    for (auto it = vec.begin(); it != vec.end(); ++it) {
      if (it->sender == sender) {
        return const_cast<StageT*>(&(*it));
      }
    }
    return nullptr;
  }

  //// -= THIRD SOLVER CLASS DATA FIELDS =-
  std::array<uint8_t, Consensus::MaxTrustedNodes> markUntrusted;

  std::vector<cs::StageOne> stageOneStorage;
  std::vector<cs::StageTwo> stageTwoStorage;
  std::vector<cs::StageThree> stageThreeStorage;
  std::vector<cs::StageThree> trueStageThreeStorage;
  bool realTrustedChanged_;
  cs::Bytes tempRealTrusted_;

  std::vector <std::pair<uint8_t, cs::Signature>> newBlockSignatures;

  std::vector <int> smartUntrusted;
  std::vector <csdb::Pool::SmartSignature> solverSmartSignatures_;
  // stores candidates for next round
  std::vector<cs::PublicKey> trusted_candidates;
  std::vector <cs::TransactionsPacketHash> hashes_candidates;
  csdb::Pool deferredBlock_;
  uint8_t currentStage3iteration_ = 0;

  // tracks round info missing ("last hope" tool)
  TimeoutTracking track_next_round;
  std::vector<cs::SmartConsensus> smartProcesses_;
  const size_t simultaneuosSmartsNumber_ = 20;
};

}  // namespace slv2
