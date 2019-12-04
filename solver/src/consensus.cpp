#include <consensus.hpp>

/** @brief   Set the flag to log solver-with-state messages to console*/
constexpr bool Consensus::Log = true;

/** @brief   True if re-select write node on timeout is enabled*/
constexpr bool Consensus::ReSelectWriteOnTimeout = false;

/** @brief   True if write node may to reduce desired count of hashes on big bang and spawn next round immediately*/
constexpr bool Consensus::ReduceMinHashesOnBigBang = false;

/** @brief   The default state timeout */
constexpr unsigned int Consensus::DefaultStateTimeout = 5000;

/** @brief   The default state timeout */
constexpr uint64_t Consensus::DefaultTimeStampRange = 30000;

/** @brief   Maximum time in msec to wait new round after consensus achieved, after that waiting trusted nodes
 * activates */
constexpr unsigned int Consensus::PostConsensusTimeout = 60000;

/** @brief   Maximum round duration when the transaction input is allowed - used to avoid BlackList */
constexpr size_t Consensus::MaxRoundDuration = 300000;

/** @brief   The minimum cash for trusted nodes to participate consensus */
constexpr csdb::Amount Consensus::MinStakeValue = csdb::Amount{50000};

/** @brief   The round when DPOS starts working */
constexpr uint64_t Consensus::StartingDPOS = 10'000ULL;

/** @brief   The return value means: general (Writer->General) is not selected by "generals" */
constexpr uint8_t Consensus::GeneralNotSelected = 100;

/** @brief   The return value is the maximum allowed time interval (in milliseconds) for collectiong hashes */
constexpr uint64_t Consensus::MaxTimeStampDelta = 1000 * 60 * 3;

/** @brief   Min duration (msec) to collect hashes in stage-1 of consensus */
constexpr uint32_t Consensus::TimeMinStage1 = 500;

/** @brief   Number of rounds to prevent node from consensus participation */
constexpr uint32_t Consensus::GrayListPunishment = 1000;

/** @brief   Number of node working rounds to start checking roundPackage ctreating speed */
constexpr uint64_t Consensus::SpeedCheckRound = 1000;

/** @brief   Max duration (msec) of the whole round (SolverCore on the 1st round) */
constexpr uint32_t Consensus::TimeRound = 2000;

/** @brief   Max timeout (msec) to wait stages (Trusted-2,3) */
constexpr uint32_t Consensus::TimeStageRequest = 2000;

/** @brief   Max subround delta */
constexpr uint8_t Consensus::MaxSubroundDelta = 10;

/** @brief   Max subround delta */
constexpr uint64_t Consensus::MaxRoundTimerFree = 5;

/** @brief   Max timeout (msec) to execute smart contract */
constexpr uint32_t Consensus::TimeSmartContract = 60000;

/** @brief   Max time to collect transactions (PermanentWrite, SolverCore on BigBang) */
constexpr uint32_t Consensus::TimeCollTrans = 500;

/** @brief   Max hashes count to include in stage one data */
constexpr size_t Consensus::MaxStageOneHashes = 100;

/** @brief   Max distance of Utility message */
constexpr size_t Consensus::UtilityMessageRoundInterval = 50;

/** @brief   Black list counter - max number of penalty points to get to the black list */
constexpr size_t Consensus::BlackListCounterMaxValue = 100000;

/** @brief   Black list counter - amount of penalty points for one mistake */
constexpr size_t Consensus::BlackListCounterSinglePenalty = 10000;

/** @brief   Max transaction size */
constexpr size_t Consensus::MaxTransactionSize = 100 * 1024;

/** @brief   Max hashes count to include in stage one data */
constexpr size_t Consensus::MaxStageOneTransactions = 1000;

/** @brief   Max transaction's size to include in stage one data */
constexpr size_t Consensus::MaxPreliminaryBlockSize = 1 * 1024 * 1024;

/** @brief   Max transactions count in smart contract execution result, both new state and emitted ones */
constexpr size_t Consensus::MaxContractResultTransactions = 100;

/** @brief   Max transactions in the packet, the sender won't be accused for, if all them are invalid */
constexpr size_t Consensus::AccusalPacketSize = 10;

/** @brief   Max count of rounds to execute smart contract. After that contract is assumed failed unconditionally */
constexpr unsigned int Consensus::MaxRoundsCancelContract = 100;

/** @brief The maximum count of rounds to store in chain new_state transaction. If contract still is "in the executor" timeout is fixed.
 * After that 5 rounds (MaxRoundsCancelContract - MaxRoundsExecuteContract) remains to complete consensus and put timeout new_state
 * into chain, otherwise  contract is assumed failed unconditionally
 */
constexpr unsigned int Consensus::MaxRoundsExecuteContract = 95;

/** @brief True to disable, false to enable the trusted request to become trusted next round again */
constexpr bool Consensus::DisableTrustedRequestNextRound = false;

/** The max contract's state size in bytes to synchronize it between node via conveyer. Otherwise, every node must get new state
itself or make individual request to dedicated trusted nodes*/
constexpr size_t Consensus::MaxContractStateSizeToSync = 8192;
