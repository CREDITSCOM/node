#include <states/NormalState.h>
#include <SolverContext.h>
#include <Consensus.h>

#pragma warning(push)
//#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/blockchain.hpp>
#pragma warning(pop)

#include <csdb/address.h>
#include <csdb/currency.h>
#include <csdb/amount.h>
#include <csdb/amount_commission.h>
#include <lib/system/logger.hpp>

#pragma warning(push)
#pragma warning(disable: 4324)
#include <sodium.h>
#pragma warning(pop)

namespace slv2
{

    void NormalState::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        // if we were Writer un the previous round, we have a deferred block, flush it:
        if(context.is_block_deferred()) {
            context.flush_deferred_block();
        }
    }

    Result NormalState::onBlock(SolverContext & context, csdb::Pool & block, const cs::PublicKey & sender)
    {
        auto r = DefaultStateBehavior::onBlock(context, block, sender);
        if(context.is_block_deferred()) {
            context.flush_deferred_block();
        }
        return r;
    }

} // slv2
