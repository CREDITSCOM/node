#pragma once

#include "callsqueuescheduler.hpp"
#include "timeouttracking.hpp"
#include "consensus.hpp"
#include "inodestate.hpp"
#include "stage.hpp"

#include <csdb/pool.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <csnode/transactionspacket.hpp>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

// forward declarations
class Node;
namespace cs
{
  class WalletsState;
}  // namespace cs

// TODO: discuss possibility to switch states after timeout expired, timeouts can be individual but controlled by
// SolverCore

namespace slv2
{

  class SolverCore
  {
  public:
    using Counter = size_t;

    SolverCore();
    explicit SolverCore(Node* pNode, csdb::Address GenesisAddress, csdb::Address StartAddress);

    ~SolverCore();

    void startDefault()
    {
      opt_debug_mode ? ExecuteStart(Event::SetNormal) : ExecuteStart(Event::Start);
    }

    void startAsMain()
    {
      ExecuteStart(Event::SetTrusted);
    }

    void finish();

    bool is_finished() const
    {
      return req_stop;
    }

    // Solver "public" interface,
    // below are the "required" methods to be implemented by Solver-compatibility issue:

    void setKeysPair(const cs::PublicKey& pub, const cs::PrivateKey& priv);
    void gotRound(cs::RoundNumber rNum);
    void gotHash(csdb::PoolHash&& hash, const cs::PublicKey& sender);
    const cs::PublicKey& getPublicKey() const
    {
      return public_key;
    }
    const cs::PrivateKey& getPrivateKey() const
    {
      return private_key;
    }
    // TODO: requires revision
    const cs::PublicKey& getWriterPublicKey() const;

    void gotBigBang();
    void gotTransaction(const csdb::Transaction& trans);
    // obsolete:
    void gotVector(const cs::HashVector&)
    {}
    // obsolete:
    void gotMatrix(cs::HashMatrix&&)
    {}
    void gotBlock(csdb::Pool&& p, const cs::PublicKey& sender);
    void gotBlockRequest(const csdb::PoolHash& p_hash);
    void gotBlockReply(csdb::Pool& p);
    // obsolete:
    void gotIncorrectBlock(csdb::Pool&&, const cs::PublicKey&)
    {}
    // obsolete:
    void gotFreeSyncroBlock(csdb::Pool&&)
    {}
    // obsolete:
    void rndStorageProcessing()
    {}
    // obsolete:
    void tmpStorageProcessing()
    {}
    void beforeNextRound();
    void nextRound();
    void gotRoundInfoRequest(const cs::PublicKey& requester, cs::RoundNumber requester_round);
    void gotRoundInfoReply(bool next_round_started, const cs::PublicKey& respondent);

    // Solver3 "public" extension
    void gotStageOne(const cs::StageOne& stage);
    void gotStageTwo(const cs::StageTwo& stage);
    void gotStageThree(const cs::StageThree& stage);

    void gotStageOneRequest(uint8_t requester, uint8_t required);
    void gotStageTwoRequest(uint8_t requester, uint8_t required);
    void gotStageThreeRequest(uint8_t requester, uint8_t required);

    /// <summary>   Adds a transaction passed to send pool </summary>
    ///
    /// <remarks>   Aae, 14.10.2018. </remarks>
    ///
    /// <param name="tr">   The transaction </param>

    void send_wallet_transaction(const csdb::Transaction& tr);

    // obsolete:
    csdb::Pool::sequence_t getNextMissingBlock(const uint32_t /*starting_after*/) const
    {
      return 0;
    }

    // empty in Solver
    void gotBadBlockHandler(const csdb::Pool& /*p*/, const cs::PublicKey& /*sender*/) const
    {}

  private:
    // to use private data while serve for states as SolverCore context:
    friend class SolverContext;

    enum class Event
    {
      Start,
      BigBang,
      RoundTable,
      Transactions,
      Block,
      Hashes,
      Stage1Enough,
      Stage2Enough,
      Stage3Enough,
      SyncData,
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

    /** @brief   True if option is permanent node roles is on */
    bool opt_debug_mode;

    // inner data

    std::unique_ptr<SolverContext> pcontext;
    CallsQueueScheduler scheduler;
    CallsQueueScheduler::CallTag tag_state_expired;
    bool req_stop;
    std::map<StatePtr, Transitions> transitions;
    StatePtr pstate;
    size_t cnt_trusted_desired;
    // amount of transactions received (to verify or not or to ignore)
    size_t total_recv_trans;
    // amount of accepted transactions (stored in blockchain)
    size_t total_accepted_trans;
    // amount of deferred transactions (in deferred block)
    size_t cnt_deferred_trans;
    std::chrono::steady_clock::time_point t_start_ms;
    size_t total_duration_ms;

    // consensus data

    csdb::Address addr_genesis;
    csdb::Address addr_start;
    size_t cur_round;
    cs::PublicKey public_key;
    cs::PrivateKey private_key;
    // senders of hashes received this round
    std::vector<std::pair<csdb::PoolHash, cs::PublicKey>> recv_hash;
    // pool for storing individual transactions from wallets and candidates for transaction list,
    // good transactions storage, serve as source for new block
    csdb::Pool accepted_pool {};
    // to store outrunning blocks until the time to insert them comes

    // previous solver version instance

    Node* pnode;
    std::unique_ptr<cs::WalletsState> pws;

    void ExecuteStart(Event start_event);

    void InitTransitions();
    void InitDebugModeTransitions();
    void setState(const StatePtr& pState);

    void handleTransitions(Event evt);
    bool stateCompleted(Result result);

    void spawn_next_round(const std::vector<cs::PublicKey>& nodes);
    void store_received_block(csdb::Pool& p, bool defer_write);
    bool is_block_deferred() const;
    void flush_deferred_block();
    void drop_deferred_block();

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

    cs::StageOne* find_stage1(uint8_t sender)
    {
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

    cs::StageTwo* find_stage2(uint8_t sender)
    {
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

    cs::StageThree* find_stage3(uint8_t sender)
    {
      return find_stage<>(stageThreeStorage, sender);
    }

    const cs::StageThree* find_stage3(uint8_t sender) const
    {
      return find_stage<>(stageThreeStorage, sender);
    }

    template <typename StageT>
    StageT* find_stage(const std::vector<StageT>& vec, uint8_t sender) const
    {
      for(auto it = vec.begin(); it != vec.end(); ++it) {
        if(it->sender == sender) {
          return (StageT*)&(*it);
        }
      }
      return nullptr;
    }

    //// -= THIRD SOLVER CLASS DATA FIELDS =-
    std::array<uint8_t, Consensus::MaxTrustedNodes> markUntrusted;

    std::vector<cs::StageOne> stageOneStorage;
    std::vector<cs::StageTwo> stageTwoStorage;
    std::vector<cs::StageThree> stageThreeStorage;

    // stores candidates for next round
    std::vector<cs::PublicKey> trusted_candidates;

    // tracks round info missing ("last hope" tool)
    TimeoutTracking track_next_round;
  };

}  // namespace slv2
