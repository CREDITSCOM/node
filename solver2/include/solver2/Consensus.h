#pragma once

#include <cstdint>

class Consensus
{
public:

    constexpr static unsigned int DefaultStateTimeout = 5000;

    constexpr static unsigned int MinTrustedNodes = 3;

    /** @brief   Max duration (msec) of the whole round (N, W, G, T) */
    constexpr static uint32_t T_round = 2000;

    /** @brief   Max timeout (msec) to wait next round table (N, W, G, T) */
    constexpr static uint32_t T_rt = 300;

    /** @brief   Max timeout (msec) to wait transaction list (T) */
    constexpr static uint32_t T_tl = 200;

    /** @brief   Max timeout (msec) to wait all vectors (T) */
    constexpr static uint32_t T_vec = 400;

    /** @brief   Max timeout (msec) to wait all matrices (T) */
    constexpr static uint32_t T_mat = 600;

    /** @brief   Max timeout (msec) to wait block (N, G, T) */
    constexpr static uint32_t T_blk = 400;

    /** @brief   Max timeout (msec) to wait hashes after write & send block (W) */
    constexpr static uint32_t T_hash = 400;


    /** @brief   Max time to collect transactions (G) */
    constexpr static uint32_t T_coll_trans = 200;

    /** @brief   Period between flush transactions (N) */
    constexpr static uint32_t T_flush_trans = 50;

};
