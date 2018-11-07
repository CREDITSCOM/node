#include "SolverContext.h"
#include "SolverCore.h"
#include "Node.h"
#include <lib/system/logger.hpp>

namespace slv2
{
	BlockChain& SolverContext::blockchain() const
	{
		return core.pnode->getBlockChain();
	}

    size_t SolverContext::own_conf_number() const
    {
        return (size_t) core.pnode->getConfidantNumber();
    }

    size_t SolverContext::cnt_trusted() const
    {
        return 4;//core.pnode->getConfidants().size(); // vshilkin
    }

    const std::vector<cs::PublicKey>& SolverContext::trusted() const
    {
//        return core.pnode->getConfidants(); // vshilkin
      static std::vector<cs::PublicKey> tmp{};
      return tmp;
    }

    void SolverContext::request_round_table() const
    {
        core.pnode->sendRoundTableRequest(core.cur_round);
    }

    Role SolverContext::role() const
    {
        auto v = core.pnode->getNodeLevel();
        switch(v) {
        case NodeLevel::Normal:
            return Role::Normal;
        case NodeLevel::Confidant:
            return Role::Trusted;
        case NodeLevel::Main:
            return Role::Collect;
        case NodeLevel::Writer:
            return Role::Write;
        default:
            break;
        }
        LOG_ERROR("SolverCore: unknown NodelLevel value " << static_cast<int>(v) << " was returned by Node");
        //TODO: how to handle "unknown" node level value?
        return Role::Normal;
    }

    void SolverContext::spawn_next_round()
    {
        //core.pnode->initNextRound(core.pnode->getMyPublicKey(), std::move(core.recv_hash)); //vshilkin
    }

    csdb::Address SolverContext::optimize(const csdb::Address& address) const
    {
        csdb::internal::WalletId id;
        if (core.pnode->getBlockChain().findWalletId(address, id)) {
            return csdb::Address::from_wallet_id(id);
        }
        return address;
    }

    void SolverContext::send_hash(const cs::Hash & hash, const cs::PublicKey & target)
    {
//        core.pnode->sendHash(hash, target); // vshilkin
    }

    void SolverContext::send_own_vector()
    {
        core.pnode->sendVector(core.getMyVector());
    }

    void SolverContext::send_own_matrix()
    {
        core.pnode->sendMatrix(core.getMyMatrix());
    }

    void SolverContext::send_transaction_list(csdb::Pool & pool)
    {
    }

} // slv2
