#include <consensus.hpp>
#include <solvercontext.hpp>
#include <states/normalstate.hpp>

#pragma warning(push)
//#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/blockchain.hpp>
#pragma warning(pop)

#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/amount_commission.hpp>
#include <csdb/currency.hpp>
#include <lib/system/logger.hpp>

namespace cs {

void NormalState::on(SolverContext& context) {
  DefaultStateBehavior::on(context);
}

}  // namespace slv2
