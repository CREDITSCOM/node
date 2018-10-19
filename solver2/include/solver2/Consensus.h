#pragma once

#include <cstdint>

class Consensus
{
public:


    /** @brief   Set the flag to log solver-with-state messages to console*/
    constexpr static bool Log = true;

    /** @brief   The default state timeout, currently not used */
    constexpr static unsigned int DefaultStateTimeout = 5000;

    /** @brief   The minimum trusted nodes to start consensus */
    constexpr static unsigned int MinTrustedNodes = 3;

    /** @brief   The return value means: general (Writer->General) is not selected by "generals" */
    constexpr static uint8_t GeneralNotSelected = 100;

    /** @brief   Max duration (msec) of the whole round (N, W, G, T) */
    constexpr static uint32_t T_round = 2000;

    /** @brief   Max timeout (msec) to wait next round table (N, W, G, T) */
    //constexpr static uint32_t T_rt = 300;

    /** @brief   Max timeout (msec) to wait transaction list (T) */
    //constexpr static uint32_t T_tl = 200;

    /** @brief   Max timeout (msec) to wait all vectors, then all matrices (T) */
    constexpr static uint32_t T_consensus = 2500;

    /** @brief   Max timeout (msec) to wait block (N, G, T) */
    //constexpr static uint32_t T_blk = 400;

    /** @brief   Max timeout (msec) to wait hashes after write & send block (W) */
    constexpr static uint32_t T_hash = 400;

    /** @brief   Max time to collect transactions (G) */
    constexpr static uint32_t T_coll_trans = 500;

    /** @brief   Period between flush transactions (N) */
    constexpr static uint32_t T_flush_trans = 200;

};
