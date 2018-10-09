#include "SolverContext.h"
#include "SolverCore.h"
#include "Node.h"

namespace slv2
{
	BlockChain& SolverContext::blockchain() const
	{
		return core.pnode->getBlockChain();
	}

    uint8_t SolverContext::own_conf_number() const
    {
        return core.pnode->getMyConfNumber();
    }

    size_t SolverContext::cnt_trusted() const
    {
        return core.pnode->getConfidants().size();
    }

    void SolverContext::spawn_next_round()
    {
        core.pnode->initNextRound(core.pnode->getMyPublicKey(), std::move(core.recv_hash));
    }

	csdb::Address SolverContext::optimize(const csdb::Address& address) const
	{
		csdb::internal::WalletId id;
		if (core.pnode->getBlockChain().findWalletId(address, id)) {
			return csdb::Address::from_wallet_id(id);
		}
		return address;
	}

} // slv2
