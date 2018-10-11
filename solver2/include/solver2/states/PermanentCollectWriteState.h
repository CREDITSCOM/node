#pragma once
#include "CollectState.h"
#include "../Consensus.h"
#include "../CallsQueueScheduler.h"

namespace slv2
{
    /**
     * @class   PermanentCollectWriteState
     *
     * @brief   A permanent collect write state. This class cannot be inherited. It is used in special testing mode to serve at the same time
     *          as Collector (main node) and Writer (write node) during the same round
     *
     * @author  Alexander Avramenko
     * @date    11.10.2018
     */

    class PermanentCollectWriteState final : public CollectState
    {
    public:

        ~PermanentCollectWriteState() override
        {}

        void on(SolverContext& context) override;

        // suppress sending TL on round end
        void onRoundEnd(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;

        const char * name() const override
        {
            return "PermCollWrite";
        }

    private:

        void create_and_send_block(SolverContext& context);
        void start_collect_transactions(SolverContext& context);
        void cancel_collect_transactions(SolverContext& context);
        void start_collect_hashes(SolverContext& context);
        void cancel_collect_hashes(SolverContext& context);

        /** @brief   The pointer to own hash actual this round */
        std::unique_ptr<Hash> pown;

        /** @brief   Minimal required count of hashes in reply after send block */
        int min_count_hashes { static_cast<int>(Consensus::MinTrustedNodes) };

        /** @brief   Timeout control */
        CallsQueueScheduler::CallTag tag_round_timeout { CallsQueueScheduler::no_tag };
        CallsQueueScheduler::CallTag tag_hashes_timeout { CallsQueueScheduler::no_tag };
    };

} // slv2
