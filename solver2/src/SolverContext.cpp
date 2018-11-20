#include "SolverContext.h"
#include "SolverCore.h"

#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/node.hpp>
#include <csnode/conveyer.hpp>
#pragma warning(pop)

#include <lib/system/logger.hpp>

namespace slv2
{
	BlockChain& SolverContext::blockchain() const
	{
		return core.pnode->getBlockChain();
	}

    void SolverContext::add_stage1(cs::StageOne & stage, bool send)
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

    void SolverContext::add_stage2(cs::StageTwo& stage, bool send)
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

    void SolverContext::add_stage3(cs::StageThree& stage)
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
        return (size_t) core.pnode->getConfidantNumber();
    }

    size_t SolverContext::cnt_trusted() const
    {
        return cs::Conveyer::instance().roundTable().confidants.size(); //core.pnode->getConfidants().size();
    }

    const std::vector<cs::PublicKey>& SolverContext::trusted() const
    {
        return cs::Conveyer::instance().roundTable().confidants;
    }

    void SolverContext::request_round_table() const
    {
//        core.pnode->sendRoundTableRequest(core.cur_round);
    }

    Role SolverContext::role() const
    {
        auto v = core.pnode->getNodeLevel();
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
        LOG_ERROR("SolverCore: unknown NodeLevel value " << static_cast<int>(v) << " was returned by Node");
        //TODO: how to handle "unknown" node level value?
        return Role::Normal;
    }

    void SolverContext::spawn_next_round()
    {
        LOG_NOTICE("SolverCore: spawn next round");
        if(Consensus::Log) {
            if(core.trusted_candidates.empty()) {
                cserror() << "SolverCore: trusted candidates list must not be empty while spawn next round";
            }
        }
        cslog() << "SolverCore: new confidant nodes: ";
        int i = 0;
        for(auto& it : core.trusted_candidates) {
            cslog() << '\t' << i << ". " << cs::Utils::byteStreamToHex(it.data(), it.size());
            ++i;
        }
        uint8_t own_num = (uint8_t) own_conf_number();
        const auto ptr = stage3(own_num);
        if(ptr != nullptr && ptr->writer == own_num) {
            switch(round()) {
                case 10:
                case 20:
                case 30:
                    return;
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
        int i = 0;
        for (auto& it : core.trusted_candidates) {
          std::cout << i << ". " << cs::Utils::byteStreamToHex(it.data(), it.size()) << std::endl;
          ++i;
        }

        core.pnode->initNextRound(std::move(core.trusted_candidates));
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
        csdb::internal::byte_array bytes(hash.cbegin(), hash.cend());
        core.pnode->sendHash(csdb::PoolHash::from_binary(bytes), target);
    }

    bool SolverContext::test_trusted_idx(uint8_t idx, const cs::PublicKey & sender)
    {
        // vector<Hash> confidantNodes_ in Node actually stores PublicKey items :-)
        const auto& trusted = this->trusted();
        if(idx < trusted.size()) {
            const auto& pk = *(trusted.cbegin() + idx);
            return 0 == memcmp(pk.data(), sender.data(), pk.size());
        }
        return false;
    }

    csdb::internal::byte_array SolverContext::last_block_hash() const
    {
        //if(!core.is_block_deferred()) {
            return core.pnode->getBlockChain().getLastWrittenHash().to_binary();
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

    void SolverContext::update_fees(cs::TransactionsPacket& p)
    {
        core.pfee->CountFeesInPool(blockchain(), &p);
    }

    bool SolverContext::transaction_still_in_pool(int64_t inner_id) const
    {
        cs::Lock lock(cs::Conveyer::instance().sharedMutex());

        const auto& block = cs::Conveyer::instance().transactionsBlock();
        for(const auto& packet: block) {
            for(const auto& tr : packet.transactions()) {
                if(tr.innerID() == inner_id) {
                    return true;
                }
            }
        }
        return false;
    }

    void SolverContext::request_round_info(uint8_t respondent1, uint8_t respondent2)
    {
        cslog() << "SolverCore: ask [" << (int) respondent1 << "] for RoundInfo";
        core.pnode->sendRoundInfoRequest(respondent1);
        cslog() << "SolverCore: ask [" << (int) respondent2 << "] for RoundInfo";
        core.pnode->sendRoundInfoRequest(respondent2);
    }

} // slv2
