#include "SolverContext.h"
#include "SolverCore.h"

#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)

#include <lib/system/logger.hpp>

namespace slv2
{
	BlockChain& SolverContext::blockchain() const
	{
		return core.pnode->getBlockChain();
	}

    void SolverContext::add_stage1(Credits::StageOne & stage, bool send)
    {
        //core.stageOneStorage.push_back(stage);
        if(send) {
            core.pnode->sendStageOne(stage);
        }
        /*the order is important! the signature is created in node 
        before sending stage and then is inserted in the field .sig
        now we can add it to stages storage*/
        core.gotStageOne(stage);
    }

    void SolverContext::add_stage2(Credits::StageTwo& stage, bool send)
    {
        //core.stageTwoStorage.push_back(stage);

        if(send) {
            core.pnode->sendStageTwo(stage);
        }
        /*the order is important! the signature is created in node
        before sending stage and then is inserted in the field .sig
        now we can add it to stages storage*/
        core.gotStageTwo(stage);
    }

    void SolverContext::add_stage3(Credits::StageThree& stage)
    {
        //core.stageThreeStorage.push_back(stage);

        core.pnode->sendStageThree(stage);
        /*the order is important! the signature is created in node
        before sending stage and then is inserted in the field .sig
        now we can add it to stages storage*/
        core.gotStageThree(stage);
    }

    size_t SolverContext::own_conf_number() const
    {
        return (size_t) core.pnode->getMyConfNumber();
    }

    size_t SolverContext::cnt_trusted() const
    {
        return core.pnode->getConfidants().size();
    }

    const std::vector<PublicKey>& SolverContext::trusted() const
    {
        return core.pnode->getConfidants();
    }

    void SolverContext::request_round_table() const
    {
        core.pnode->sendRoundTableRequest(core.cur_round);
    }

    Role SolverContext::role() const
    {
        auto v = core.pnode->getMyLevel();
        switch(v) {
        case NodeLevel::Normal:
        case NodeLevel::Main:
            return Role::Normal;
        case NodeLevel::Confidant:
        case NodeLevel::Writer:
            return Role::Trusted;
        default:
            break;
        }
        LOG_ERROR("SolverCore: unknown NodelLevel value " << static_cast<int>(v) << " was returned by Node");
        //TODO: how to handle "unknown" node level value?
        return Role::Normal;
    }

    void SolverContext::spawn_next_round()
    {
        if(Consensus::Log) {
            if(core.trusted_candidates.empty()) {
                LOG_ERROR("SolverCore: trusted candidates list must not be empty while spawn next round");
            }
        }
        core.spawn_next_round(core.trusted_candidates);
    }

    void SolverContext::spawn_first_round()
    {
        if(core.trusted_candidates.empty()) {
            if(Consensus::Log) {
                LOG_ERROR("SolverCore: trusted candidates must be " << Consensus::MinTrustedNodes << " or greater to spawn first round");
            }
            return;
        }
        core.pnode->initNextRound(*core.trusted_candidates.cbegin(), std::move(core.trusted_candidates));
    }

    csdb::Address SolverContext::optimize(const csdb::Address& address) const
	{
		csdb::internal::WalletId id;
		if (core.pnode->getBlockChain().findWalletId(address, id)) {
			return csdb::Address::from_wallet_id(id);
		}
		return address;
	}

    void SolverContext::send_hash(const Hash & hash, const PublicKey & target)
    {
        core.pnode->sendHash(hash, target);
    }

    bool SolverContext::test_trusted_idx(uint8_t idx, const PublicKey & sender)
    {
        // vector<Hash> confidantNodes_ in Node actually stores PublicKey items :-)
        const auto& trusted = core.pnode->getConfidants();
        if(idx < trusted.size()) {
            const auto& pk = *(trusted.cbegin() + idx);
            return 0 == memcmp(pk.str, sender.str, sizeof(PublicKey));
        }
        return false;
    }

    const uint8_t* SolverContext::last_block_hash()
    {
        //if(!core.is_block_deferred()) {
            return core.pnode->getBlockChain().getLastWrittenHash().to_binary().data();
        //}
        //return core.deferred_block.hash().to_binary().data();
    }

    void SolverContext::request_stage1(uint8_t from, uint8_t required)
    {
        LOG_NOTICE("SolverCore: ask [" << (int) from << "] for stage-1 of [" << (int) required << "]");
        core.pnode->requestStageOne(from, required);
    }

    void SolverContext::request_stage2(uint8_t from, uint8_t required)
    {
        LOG_NOTICE("SolverCore: ask [" << (int) from << "] for stage-2 of [" << (int) required << "]");
        core.pnode->requestStageTwo(from, required);
    }

    void SolverContext::request_stage3(uint8_t from, uint8_t required)
    {
      LOG_NOTICE("SolverCore: ask [" << (int)from << "] for stage-3 of [" << (int)required << "]");
      core.pnode->requestStageThree(from, required);
    }

} // slv2
