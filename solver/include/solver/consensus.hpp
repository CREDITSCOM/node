#pragma once

#include <csdb/amount.hpp>
#include <cstddef>  // : for size_t
#include <cstdint>

class Consensus {
public:
    /** @brief   Set the flag to log solver-with-state messages to console*/
    const static bool Log;

    /** @brief   True if re-select write node on timeout is enabled*/
    const static bool ReSelectWriteOnTimeout;

    /** @brief   True if write node may to reduce desired count of hashes on big bang and spawn next round immediately*/
    const static bool ReduceMinHashesOnBigBang;

    /** @brief   The default state timeout */
    const static unsigned int DefaultStateTimeout;

    /** @brief   The default state timeout */
    const static uint64_t DefaultTimeStampRange;

    /** @brief   The minimum state timeout */
    constexpr static uint64_t MinimumTimeStampRange = 300;

    /** @brief   Maximum time in msec to wait new round after consensus achieved, after that waiting trusted nodes
     * activates */
    const static unsigned int PostConsensusTimeout;

    /** @brief   Maximum round duration when the transaction input is allowed - used to avoid BlackList */
    const static size_t MaxRoundDuration;

    /** @brief   The minimum trusted nodes to start consensus */
    const static unsigned int MinTrustedNodes = 4;

    /** @brief   The maximum trusted nodes to take part in consensus */
    const static unsigned int MaxTrustedNodes = 25;

    /** @brief   The minimum cash for trusted nodes to participate consensus */
    const static csdb::Amount MinStakeValue;

    /** @brief   The minimum stake to be delegated to another node */
    constexpr static csdb::Amount MinStakeDelegated = csdb::Amount{ 5 };

    /** @brief   The round when DPOS starts working */
    const static uint64_t StartingDPOS;

    /** @brief   The return value means: general (Writer->General) is not selected by "generals" */
    const static uint8_t GeneralNotSelected;

    /** @brief   The return value is the maximum allowed time interval (in milliseconds) for collectiong hashes */
    const static uint64_t MaxTimeStampDelta;

    /** @brief   Min duration (msec) to collect hashes in stage-1 of consensus */
    const static uint32_t TimeMinStage1;

    /** @brief   Number of rounds to prevent node from consensus participation */
    const static uint32_t GrayListPunishment;

    /** @brief   Number of node working rounds to start checking roundPackage ctreating speed */
    const static uint64_t SpeedCheckRound;

    /** @brief   Max duration (msec) of the whole round (SolverCore on the 1st round) */
    const static uint32_t TimeRound;

    /** @brief   Max timeout (msec) to wait stages (Trusted-2,3) */
    const static uint32_t TimeStageRequest;

    /** @brief   Max subround delta */
    const static uint8_t MaxSubroundDelta;

    /** @brief   Max subround delta */
    const static uint64_t MaxRoundTimerFree;

    /** @brief   Max timeout (msec) to execute smart contract */
    const static uint32_t TimeSmartContract;

    /** @brief   Max time to collect transactions (PermanentWrite, SolverCore on BigBang) */
    const static uint32_t TimeCollTrans;

    /** @brief   Max hashes count to include in stage one data */
    const static size_t MaxStageOneHashes;

    /** @brief   Max distance of Utility message */
    const static size_t UtilityMessageRoundInterval;

    /** @brief   Black list counter - max number of penalty points to get to the black list */
    const static size_t BlackListCounterMaxValue;

    /** @brief   Black list counter - amount of penalty points for one mistake */
    const static size_t BlackListCounterSinglePenalty;

    /** @brief   Max transaction size */
    const static size_t MaxTransactionSize;

    /** @brief   Max hashes count to include in stage one data */
    const static size_t MaxStageOneTransactions;

    /** @brief   Max transaction's size to include in stage one data */
    const static size_t MaxPreliminaryBlockSize;

    /** @brief   Max transactions count in smart contract execution result, both new state and emitted ones */
    const static size_t MaxContractResultTransactions;

    /** @brief   Max transactions in the packet, the sender won't be accused for, if all them are invalid */
    const static size_t AccusalPacketSize;

    /** @brief   Max count of rounds to execute smart contract. After that contract is assumed failed unconditionally */
    const static unsigned int MaxRoundsCancelContract;

    /** @brief The maximum count of rounds to store in chain new_state transaction. If contract still is "in the executor" timeout is fixed.
     * After that, 90 rounds (MaxRoundsCancelContract - MaxRoundsExecuteContract) still remains to complete consensus and put empty new_state
     * into chain, otherwise  contract is assumed failed unconditionally
     */
    const static unsigned int MaxRoundsExecuteContract;

    /** @brief True to disable, false to enable the trusted request to become trusted next round again */
    const static bool DisableTrustedRequestNextRound;

    /** The max contract's state size in bytes to synchronize it between node via conveyer. Otherwise, every node must get new state
    itself or make individual request to dedicated trusted nodes*/
    const static size_t MaxContractStateSizeToSync;
};
