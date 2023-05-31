#include <consensus.hpp>

/** @brief   Set the flag to log solver-with-state messages to console*/
const bool Consensus::Log = true;

/** @brief   True if re-select write node on timeout is enabled*/
const bool Consensus::ReSelectWriteOnTimeout = false;

/** @brief   True if write node may to reduce desired count of hashes on bootstrap and spawn next round immediately*/
const bool Consensus::ReduceMinHashesOnBootstrap = false;

/** @brief   The default state timeout */
const unsigned int Consensus::DefaultStateTimeout = 5000;

/** @brief   The default state timeout */
const uint64_t Consensus::DefaultTimeStampRange = 30000;

/** @brief   Stage One maximum size */
uint64_t Consensus::StageOneMaximumSize = 36000;

/** @brief   Maximum time in msec to wait new round after consensus achieved, after that waiting trusted nodes
 * activates */
const unsigned int Consensus::PostConsensusTimeout = 10000;

/** @brief   Maximum round duration when the transaction input is allowed - used to avoid BlackList */
const size_t Consensus::MaxRoundDuration = 300000;

/** @brief   The minimum cash for trusted nodes to participate consensus */
csdb::Amount Consensus::MinStakeValue = csdb::Amount{50000};

csdb::Amount Consensus::MaxStakeValue = csdb::Amount{ 500000 };

/** @brief   The round when DPOS starts working */
const uint64_t Consensus::StartingDPOS = 300;//10'000ULL;

/** @brief   The return value means: general (Writer->General) is not selected by "generals" */
const uint8_t Consensus::GeneralNotSelected = 100;

/** @brief   The return value is the maximum allowed time interval (in milliseconds) for collectiong hashes */
const uint64_t Consensus::MaxTimeStampDelta = 1000 * 60 * 3;

/** @brief   Min duration (msec) to collect hashes in stage-1 of consensus */
uint32_t Consensus::TimeMinStage1 = 500;

/** @brief   Number of rounds to prevent node from consensus participation */
uint32_t Consensus::GrayListPunishment = 1000;

size_t Consensus::MaxPacketsPerRound = 1000;

size_t Consensus::MaxPacketTransactions = 1000;

size_t Consensus::MaxQueueSize = 1000000;

/** @brief   Number of node working rounds to start checking roundPackage ctreating speed */
const uint64_t Consensus::SpeedCheckRound = 1000;

/** @brief   Max duration (msec) of the whole round (SolverCore on the 1st round) */
const uint32_t Consensus::TimeRound = 2000;

/** @brief   Max timeout (msec) to wait stages (Trusted-2,3) */
const uint32_t Consensus::TimeStageRequest = 2000;

/** @brief   Max subround delta */
const uint8_t Consensus::MaxSubroundDelta = 10;

/** @brief   Max subround delta */
const uint64_t Consensus::MaxRoundTimerFree = 5;

/** @brief   Max timeout (msec) to execute smart contract */
const uint32_t Consensus::TimeSmartContract = 60000;

/** @brief   Max time to collect transactions (PermanentWrite, SolverCore on Bootstrap) */
const uint32_t Consensus::TimeCollTrans = 500;

/** @brief   Max hashes count to include in stage one data */
size_t Consensus::MaxStageOneHashes = 100;

/** @brief   Max distance of Utility message */
const size_t Consensus::UtilityMessageRoundInterval = 50;

/** @brief   Black list counter - max number of penalty points to get to the black list */
const size_t Consensus::BlackListCounterMaxValue = 100000;

/** @brief   Black list counter - amount of penalty points for one mistake */
const size_t Consensus::BlackListCounterSinglePenalty = 10000;

/** @brief   Max transaction size */
size_t Consensus::MaxTransactionSize = 100 * 1024;

/** @brief   Max hashes count to include in stage one data */
size_t Consensus::MaxStageOneTransactions = 1000;

/** @brief   Max transaction's size to include in stage one data */
size_t Consensus::MaxPreliminaryBlockSize = 1 * 1024 * 1024;

/** @brief   Max transactions count in smart contract execution result, both new state and emitted ones */
const size_t Consensus::MaxContractResultTransactions = 100;

/** @brief   Max transactions in the packet, the sender won't be accused for, if all them are invalid */
const size_t Consensus::AccusalPacketSize = 10;

/** @brief   Max count of rounds to execute smart contract. After that contract is assumed failed unconditionally */
const unsigned int Consensus::MaxRoundsCancelContract = 100;

/** @brief The maximum count of rounds to store in chain new_state transaction. If contract still is "in the executor" timeout is fixed.
 * After that 5 rounds (MaxRoundsCancelContract - MaxRoundsExecuteContract) remains to complete consensus and put timeout new_state
 * into chain, otherwise  contract is assumed failed unconditionally
 */
unsigned int Consensus::MaxRoundsExecuteContract = 10;

/** @brief True to disable, false to enable the trusted request to become trusted next round again */
const bool Consensus::DisableTrustedRequestNextRound = false;

/** The max contract's state size in bytes to synchronize it between node via conveyer. Otherwise, every node must get new state
itself or make individual request to dedicated trusted nodes*/
const size_t Consensus::MaxContractStateSizeToSync = 8192;

csdb::Amount Consensus::blockReward = csdb::Amount{ 1 };

csdb::Amount Consensus::miningCoefficient = csdb::Amount{ 0 };

//Attention! before turning it on you should add a table of rounds where staking is on 
//to ensure that when the blocks will be rescanned from beginning the walletscache 
//results to the synchronized with other nodes values
bool Consensus::stakingOn = true; 

bool Consensus::miningOn = true;

cs::RoundNumber Consensus::syncroChangeRound = 8446744073709551615;