#pragma once

#include <gmock/gmock.h>
#include <CallsQueueScheduler.h>
#include <Stage.h>
#include <csdb/pool.h>
#include <csnode/blockchain.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>
#include <Solver/generals.hpp>
#include <Solver/Solver.hpp>
#include <Solver/WalletsState.h>
#include <csnode/conveyer.hpp>

namespace slv2
{
    class SolverContext
    {
    public:

        MOCK_METHOD2(add_stage1, void(const cs::StageOne&, bool));
        MOCK_METHOD2(add_stage2, void(const cs::StageTwo&, bool));
        MOCK_METHOD2(add_stage3, void(const cs::StageThree&, bool));
        MOCK_CONST_METHOD0(enough_stage1, bool());
        MOCK_CONST_METHOD0(enough_stage2, bool());
        MOCK_CONST_METHOD0(enough_stage3, bool());
        MOCK_METHOD0(complete_stage2, void());
        MOCK_METHOD0(complete_stage3, void());
        MOCK_CONST_METHOD2(request_stage1, void(uint8_t, uint8_t));
        MOCK_CONST_METHOD2(request_stage2, void(uint8_t, uint8_t));
        MOCK_CONST_METHOD0(stage1_data, std::vector<cs::StageOne>& ());
        MOCK_CONST_METHOD0(stage2_data, std::vector<cs::StageTwo>& ());
        MOCK_CONST_METHOD0(stage3_data, std::vector<cs::StageThree>& ());
        MOCK_CONST_METHOD1(stage1, cs::StageOne* (uint8_t));
        MOCK_CONST_METHOD1(stage2, cs::StageTwo* (uint8_t));
        MOCK_CONST_METHOD1(stage3, cs::StageThree* (uint8_t));
        MOCK_CONST_METHOD0(cnt_trusted, size_t());
        //MOCK_CONST_METHOD0(role, slv2::Role());

        MOCK_CONST_METHOD0(is_block_deferred, bool());
        MOCK_METHOD0(flush_deferred_block, void());
        MOCK_METHOD0(drop_deferred_block, void());
        MOCK_METHOD2(store_received_block, void(csdb::Pool&, bool));
        MOCK_METHOD0(last_block_hash, cs::Hash());
        MOCK_METHOD2(send_hash, void(const cs::Hash&, const cs::PublicKey&));

        MOCK_CONST_METHOD0(blockchain, BlockChain&());
        MOCK_CONST_METHOD0(wallets, cs::WalletsState&());
        MOCK_CONST_METHOD0(scheduler, slv2::CallsQueueScheduler&());
        MOCK_METHOD1(update_fees, void(cs::TransactionsPacket&));
        MOCK_METHOD1(accept_transactions, void(const csdb::Pool&));

        MOCK_CONST_METHOD0(round, size_t());
        MOCK_CONST_METHOD0(own_conf_number, uint8_t());
        MOCK_CONST_METHOD0(is_spammer, bool());
        MOCK_CONST_METHOD0(address_spammer, csdb::Address());
    };

}
