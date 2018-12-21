#pragma once

#include "SolverContext.h"
#include <states/TrustedStage1State.h>
#include <Consensus.h>
#include <csdb/pool.h>

#include <gtest/gtest.h>

struct MockData
{
    slv2::CallsQueueScheduler sched;
    BlockChain bc;
    cs::Hash hash;
    cs::Hash hash1;
    cs::Hash hash2;
    cs::Hash hash3;
    csdb::internal::byte_array pool_hash_data;
    csdb::internal::byte_array pool_hash_data_1;
    csdb::internal::byte_array pool_hash_data_2;
    csdb::internal::byte_array pool_hash_data_3;
    csdb::PoolHash pool_hash;
    csdb::PoolHash pool_hash_1;
    csdb::PoolHash pool_hash_2;
    csdb::PoolHash pool_hash_3;
    cs::StageOne stage1;
    cs::StageTwo stage2;
    cs::StageThree stage3;
    cs::PublicKey sender1;
    cs::PublicKey sender2;
    cs::PublicKey sender3;
    csdb::Pool tl;
    csdb::Pool block;
    csdb::Transaction trans;
    slv2::SolverContext context;

    std::vector<cs::StageOne> stage1_storage;
    std::vector<cs::StageTwo> stage2_storage;
    std::vector<cs::StageThree> stage3_storage;

    MockData()
    {
        std::string s("334431BE08B784BA87B153842D1B46308CC737B4D9EFD15952C3C426E599AA1E");
        std::string s1("1111111111111111111111111111111111111111111111111111111111111111");
        std::string s2("2222222222222222222222222222222222222222222222222222222222222222");
        std::string s3("3333333333333333333333333333333333333333333333333333333333333333");
        std::copy(s.cbegin(), s.cend(), hash.begin());
        std::copy(s1.cbegin(), s1.cend(), hash1.begin());
        std::copy(s2.cbegin(), s2.cend(), hash2.begin());
        std::copy(s3.cbegin(), s3.cend(), hash3.begin());
        std::string pk1("11111111111111111111111111111111");
        std::string pk2("22222222222222222222222222222222");
        std::string pk3("33333333333333333333333333333333");
        std::copy(pk1.cbegin(), pk1.cend(), sender1.begin());
        std::copy(pk2.cbegin(), pk2.cend(), sender2.begin());
        std::copy(pk3.cbegin(), pk3.cend(), sender3.begin());
        pool_hash_data.assign(std::begin(hash), std::end(hash));
        pool_hash_data_1.assign(std::begin(hash1), std::end(hash1));
        pool_hash_data_2.assign(std::begin(hash2), std::end(hash2));
        pool_hash_data_3.assign(std::begin(hash3), std::end(hash3));
        tl.set_sequence(1);
        block.set_sequence(1);
        memset(&stage1, 0, sizeof(stage1));
        stage1_storage.assign(Consensus::MinTrustedNodes, stage1);
        memset(&stage2, 0, sizeof(stage2));
        stage2_storage.assign(Consensus::MinTrustedNodes, stage2);
        memset(&stage3, 0, sizeof(stage3));
        stage3_storage.assign(Consensus::MinTrustedNodes, stage3);
        InitCalls();
    }

    void InitCalls()
    {
        using namespace ::testing;

        ON_CALL(context, is_block_deferred()).WillByDefault(Return(true));
        ON_CALL(context, blockchain()).WillByDefault(ReturnRef(bc));
        ON_CALL(context, cnt_trusted()).WillByDefault(Return(Consensus::MinTrustedNodes));
        ON_CALL(context, scheduler()).WillByDefault(ReturnRef(sched));
        ON_CALL(context, stage1_data()).WillByDefault(ReturnRef(stage1_storage));
        ON_CALL(context, stage2_data()).WillByDefault(ReturnRef(stage2_storage));
        ON_CALL(context, stage3_data()).WillByDefault(ReturnRef(stage3_storage));
        ON_CALL(context, stage1(0)).WillByDefault(Return(&stage1_storage.at(0)));
        ON_CALL(context, stage2(0)).WillByDefault(Return(&stage2_storage.at(0)));
        ON_CALL(context, stage3(0)).WillByDefault(Return(&stage3_storage.at(0)));

        //unsigned counter = 0;
        ON_CALL(bc, getLastHash())
            //.WillByDefault(Invoke([&counter, this]() -> csdb::PoolHash& {
            //    switch(++counter) {
            //    case 1:
            //        return pool_hash_1;
            //    case 2:
            //        return pool_hash_2;
            //    case 3:
            //        return pool_hash_3;
            //    default:
            //        break;
            //    }
            //    return pool_hash;
            //}));
            .WillByDefault(ReturnRef(pool_hash));

        ON_CALL(bc, getHashBySequence(0)).WillByDefault(ReturnRef(pool_hash));
        ON_CALL(bc, getHashBySequence(1)).WillByDefault(ReturnRef(pool_hash_1));
        ON_CALL(bc, getHashBySequence(2)).WillByDefault(ReturnRef(pool_hash_2));
        ON_CALL(bc, getHashBySequence(3)).WillByDefault(ReturnRef(pool_hash_3));

        ON_CALL(pool_hash, to_binary).WillByDefault(ReturnRef(pool_hash_data));

    }
};
