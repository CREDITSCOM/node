#pragma once

#include <csdb/amount.hpp>
#include <cstddef>  // : for size_t
#include <cstdint>

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

    /** @brief   The default state timeout */
    constexpr static uint64_t DefaultTimeStampRange = 30000;

    /** @brief   Maximum time in msec to wait new round after consensus achieved, after that waiting trusted nodes
     * activates */
    constexpr static unsigned int PostConsensusTimeout = 60000;

    /** @brief   Maximum round duration when the transaction input is allowed - used to avoid BlackList */
    constexpr static size_t MaxRoundDuration = 300000;

    /** @brief   The minimum trusted nodes to start consensus */
    constexpr static unsigned int MinTrustedNodes = 3;

    /** @brief   The maximum trusted nodes to take part in consensus */
    constexpr static unsigned int MaxTrustedNodes = 25;

    /** @brief   The minimum cash for trusted nodes to participate consensus */
    constexpr static csdb::Amount MinStakeValue = csdb::Amount{50000};

    /** @brief   The round when DPOS starts working */
    constexpr static uint64_t StartingDPOS = 10'000ULL;

    /** @brief   The return value means: general (Writer->General) is not selected by "generals" */
    constexpr static uint8_t GeneralNotSelected = 100;

    /** @brief   The return value is the maximum allowed time interval (in milliseconds) for collectiong hashes */
    constexpr static uint64_t MaxTimeStampDelta = 1000 * 60 * 3;

    /** @brief   Min duration (msec) to collect hashes in stage-1 of consensus */
    constexpr static uint32_t T_min_stage1 = 170;

    /** @brief   Number of rounds to prevent node from consensus participation */
    constexpr static uint32_t GrayListPunishment = 1000;

	/** @brief   Number of node working rounds to start checking roundPackage ctreating speed */
	constexpr static uint64_t SpeedCheckRound = 1000;

    /** @brief   Max duration (msec) of the whole round (SolverCore on the 1st round) */
    constexpr static uint32_t T_round = 2000;

    /** @brief   Max timeout (msec) to wait stages (Trusted-2,3) */
    constexpr static uint32_t T_stage_request = 2000;

    /** @brief   Max subround delta */
    constexpr static uint8_t MaxSubroundDelta = 10;

    /** @brief   Max subround delta */
    constexpr static uint64_t MaxRoundTimerFree = 5;

    /** @brief   Max timeout (msec) to execute smart contract */
    constexpr static uint32_t T_smart_contract = 60000;

    /** @brief   Max time to collect transactions (PermanentWrite, SolverCore on BigBang) */
    constexpr static uint32_t T_coll_trans = 500;

    /** @brief   Max hashes count to include in stage one data */
    constexpr static size_t MaxStageOneHashes = 100;

    /** @brief   Black list counter - max number of penalty points to get to the black list */
    constexpr static size_t BlackListCounterMaxValue = 100000;

    /** @brief   Black list counter - amount of penalty points for one mistake */
    constexpr static size_t BlackListCounterSinglePenalty = 10000;

    /** @brief   Max transaction size */
    constexpr static size_t MaxTransactionSize = 8000;

    /** @brief   Max hashes count to include in stage one data */
    constexpr static size_t MaxStageOneTransactions = 1000;

    /** @brief   Max transactions in the packet, the sender won't be accused for, if all them are invalid */
    constexpr static size_t AccusalPacketSize = 10;

    /** @brief   Max count of rounds to execute smart contract. After that contract is assumed failed unconditionally */
    constexpr static unsigned int MaxRoundsCancelContract = 100;

    /** @brief The maximum count of rounds to store in chain new_state transaction. If contract still is "in the executor" timeout is fixed.
     * After that 5 rounds (MaxRoundsCancelContract - MaxRoundsExecuteContract) remains to complete consensus and put timeout new_state
     * into chain, otherwise  contract is assumed failed unconditionally
     */
    constexpr static unsigned int MaxRoundsExecuteContract = 95;

    /** @brief True to disable, false to enable the trusted request to become trusted next round again */
    constexpr static bool DisableTrustedRequestNextRound = false;

    /** The max contract's state size in bytes to synchronize it between node via conveyer. Otherwise, every node must get new state
    itself or make individual request to dedicated trusted nodes*/
    constexpr static size_t MaxContractStateSizeToSync = 8192;
};
