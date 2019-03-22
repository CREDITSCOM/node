#pragma once

#include <cstdint>
#include <cstddef> // : for size_t
#include <csdb/amount.hpp>

class Consensus {
public:
  /** @brief   Set the flag to log solver-with-state messages to console*/
  constexpr static bool Log = true;

  /** @brief   True if re-select write node on timeout is enabled*/
  constexpr static bool ReSelectWriteOnTimeout = false;

  /** @brief   True if write node may to reduce desired count of hashes on big bang and spawn next round immediately*/
  constexpr static bool ReduceMinHashesOnBigBang = false;

  /** @brief   The default state timeout */
  constexpr static unsigned int DefaultStateTimeout = 5000;

  /** @brief   Maximum time in msec to wait new round after consensus achieved, after that waiting trusted nodes
   * activates */
  constexpr static unsigned int PostConsensusTimeout = 60000;

  /** @brief   The minimum trusted nodes to start consensus */
  constexpr static unsigned int MinTrustedNodes = 3;

  /** @brief   The maximum trusted nodes to take part in consensus */
  constexpr static unsigned int MaxTrustedNodes = 5;

  /** @brief   The minimum cash for trusted nodes to participate consensus */
  constexpr static csdb::Amount MinStakeValue = csdb::Amount{ 50000 };

  /** @brief   The round when DPOS starts working */
  constexpr static uint64_t StartingDPOS = 10000000ULL;

  /** @brief   The return value means: general (Writer->General) is not selected by "generals" */
  constexpr static uint8_t GeneralNotSelected = 100;

  /** @brief   Min duration (msec) to collect hashes in stage-1 of consensus */
  constexpr static uint32_t T_min_stage1 = 220;

  /** @brief   Max duration (msec) of the whole round (SolverCore on the 1st round) */
  constexpr static uint32_t T_round = 2000;

  /** @brief   Max timeout (msec) to wait stages (Trusted-2,3) */
  constexpr static uint32_t T_stage_request = 4000;

  /** @brief   Max timeout (msec) to execute smart contract */
  constexpr static uint32_t T_smart_contract = 60000;

  /** @brief   Max time to collect transactions (PermanentWrite, SolverCore on BigBang) */
  constexpr static uint32_t T_coll_trans = 500;

  /** @brief   Max hashes count to include in stage one data */
  constexpr static size_t MaxStageOneHashes = 250;

  /** @brief   Max count of rounds to execute smart contract. After that contract is assumed failed unconditionally */
  constexpr static unsigned int MaxRoundsCancelContract = 100;

  /** @brief The maximum count of rounds to store in chain new_state transaction. If contract still is "in the executor" timeout is fixed.
   * After that 5 rounds (MaxRoundsCancelContract - MaxRoundsExecuteContract) remains to complete consensus and put timeout new_state
   * into chain, otherwise  contract is assumed failed unconditionally
   */
  constexpr static unsigned int MaxRoundsExecuteContract = 95;
};
