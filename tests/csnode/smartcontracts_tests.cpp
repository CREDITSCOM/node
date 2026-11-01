#define TESTING

#include <solver/smartcontracts.hpp>
#include <solver/statefetcher.hpp>
#include "clientconfigmock.hpp"
#include "gtest/gtest.h"
#include <csdb/transaction.hpp>
#include <apihandler.hpp>
#include <base58.h>
#include <serializer.hpp>
//#include "networkmock.hpp"
//#include "nodemock.hpp"
//#include "solvermock.hpp"
//#include "transportmock.hpp"

TEST(SmartContract, basic_checks) {
    using namespace cs;
    cs::Signature dummy_sign{};

    cscrypto::Bytes bytes;
    ASSERT_TRUE(DecodeBase58("Axu1s33E9taieswhainpzjt4M5WieYNKHRfTtbpajPoc", bytes));
    csdb::Address src = csdb::Address::from_public_key(bytes);
    ASSERT_TRUE(DecodeBase58("4xho6RB8erqziK4jiyYdGX7YUXp7J2i3YoGSu96SEUJf", bytes));
    csdb::Address tgt = csdb::Address::from_public_key(bytes);

    // basic transaction
    csdb::Transaction t(1LL, src, tgt, csdb::Currency{}, csdb::Amount(0), csdb::AmountCommission(0.0), csdb::AmountCommission(0.0), dummy_sign);
    ASSERT_TRUE(t.is_valid());

    // deploy
    api::SmartContractDeploy api_deploy_data;
    api_deploy_data.__set_sourceCode("contract code");
    api::SmartContractInvocation api_deploy_invoke;
    api_deploy_invoke.__set_smartContractDeploy(api_deploy_data);
    csdb::Transaction t_deploy = t.clone();
    t_deploy.add_user_field(trx_uf::deploy::Code, cs::Serializer::serialize(api_deploy_invoke));

    // call (start)
    api::SmartContractInvocation api_call_invoke;
    api_call_invoke.__set_method("method");
    csdb::Transaction t_start = t.clone();
    t_start.add_user_field(trx_uf::start::Methods, cs::Serializer::serialize(api_call_invoke));

    // new state
    csdb::Transaction t_state = t.clone();
    t_state.add_user_field(trx_uf::new_state::Hash, std::string{});
    ASSERT_FALSE(SmartContracts::is_new_state(t_state)); // not enough feilds
    t_state.add_user_field(trx_uf::new_state::RefStart, SmartContractRef().to_user_field());
    ASSERT_TRUE(SmartContracts::is_new_state(t_state)); // ok
    t_state.add_user_field(trx_uf::new_state::RetVal, "text"); // optional
    t_state.add_user_field(trx_uf::new_state::Fee, csdb::Amount(0)); // optional

    ASSERT_TRUE(SmartContracts::is_executable(t_deploy));
    ASSERT_TRUE(SmartContracts::is_executable(t_start));
    ASSERT_FALSE(SmartContracts::is_executable(t_state));

    ASSERT_TRUE(SmartContracts::is_deploy(t_deploy));
    ASSERT_FALSE(SmartContracts::is_deploy(t_start));
    ASSERT_FALSE(SmartContracts::is_deploy(t_state));

    ASSERT_FALSE(SmartContracts::is_start(t_deploy));
    ASSERT_TRUE(SmartContracts::is_start(t_start));
    ASSERT_FALSE(SmartContracts::is_start(t_state));

    ASSERT_FALSE(SmartContracts::is_new_state(t_deploy));
    ASSERT_FALSE(SmartContracts::is_new_state(t_start));
    ASSERT_TRUE(SmartContracts::is_new_state(t_state));

}

namespace {
csdb::Address addressOf(uint8_t b) {
    return csdb::Address::from_public_key(cs::Bytes(32, b));
}

csdb::Transaction plain(csdb::Address src, csdb::Address tgt, int64_t id) {
    return csdb::Transaction(id, src, tgt, csdb::Currency{}, csdb::Amount(1), csdb::AmountCommission(0.0), csdb::AmountCommission(0.0), cs::Signature{});
}

// a new_state of contract `contract` for the execution started at ref
csdb::Transaction newState(csdb::Address contract, cs::Sequence seq, cs::Sequence index, int64_t id, char hashByte = 'h') {
    auto t = plain(contract, contract, id);
    t.add_user_field(cs::trx_uf::new_state::Hash, std::string(32, hashByte));
    cs::SmartContractRef ref;
    ref.sequence = seq;
    ref.transaction = index;
    t.add_user_field(cs::trx_uf::new_state::RefStart, ref.to_user_field());
    return t;
}
}  // namespace

// the execution an emitted transaction is charged to is the contract's latest successful execution
// in the block (step 1 of the contract state sync plan): a live node applies every new_state of the
// block before it charges the emitted transactions, so it charges that one, wherever they stand
TEST(SmartContract, EmittedTransactionIsChargedToTheLatestExecutionInTheBlock) {
    const auto A = addressOf(1), B = addressOf(2), player = addressOf(3);
    csdb::Pool block(csdb::PoolHash{}, 50);
    block.add_transaction(newState(A, 40, 0, 1));          // 0: A's execution started at 40.0
    block.add_transaction(plain(A, player, 2));            // 1: emitted by it
    block.add_transaction(newState(B, 41, 1, 3));          // 2: B's execution started at 41.1
    block.add_transaction(plain(B, player, 4));            // 3: emitted by it
    block.add_transaction(newState(A, 45, 2, 5));          // 4: A's next execution, started at 45.2
    block.add_transaction(plain(A, player, 6));            // 5: emitted by that one
    const auto failed = newState(A, 47, 0, 7, '\0');     // 6: A's last execution failed (zero hash)
    block.add_transaction(failed);
    block.add_transaction(plain(player, A, 8));            // 7: a plain transfer to A

    const auto identity = [](const csdb::Address& a) { return a; };
    // both payouts of A go to 45.2, the latest successful execution: the failed 47.0 does not count
    for (size_t i : {size_t(1), size_t(5)}) {
        const auto ref = cs::SmartContracts::emitting_new_state(block, i, A, identity);
        ASSERT_TRUE(ref.has_value());
        EXPECT_EQ(ref->sequence, 45u);
        EXPECT_EQ(ref->transaction, 2u);
    }
    const auto refB = cs::SmartContracts::emitting_new_state(block, 3, B, identity);
    ASSERT_TRUE(refB.has_value());
    EXPECT_EQ(refB->sequence, 41u);
    // a contract without a successful execution in the block: none (the caller falls back)
    csdb::Pool other(csdb::PoolHash{}, 51);
    other.add_transaction(failed);
    other.add_transaction(plain(A, player, 9));
    EXPECT_FALSE(cs::SmartContracts::emitting_new_state(other, 1, A, identity).has_value());
}

// step 2 of the contract state sync plan: the declared call and hash survive a checkpoint, an item of
// an earlier build loads without them, and staleness compares the state with the declared hash
TEST(SmartContract, DeclaredStateSurvivesCheckpointAndOldItemsLoad) {
    cs::SmartContracts::StateItem item;
    item.ref_execute.sequence = 7;
    item.ref_execute.transaction = 1;
    item.state = "state after 7.1";
    item.declared_ref = item.ref_execute;
    item.declared_hash = cscrypto::calculateHash(reinterpret_cast<const cs::Byte*>(item.state.data()), item.state.size());

    auto bytes = item.to_bytes();
    const auto back = cs::SmartContracts::StateItem::from_bytes(bytes);
    EXPECT_EQ(back.declared_ref.sequence, 7u);
    EXPECT_EQ(back.declared_ref.transaction, 1u);
    EXPECT_EQ(back.declared_hash, item.declared_hash);
    EXPECT_EQ(back.state, item.state);

    // an item without a declaration keeps the bytes of earlier builds (their checkpoints are
    // verified against hashes of these bytes)
    cs::SmartContracts::StateItem plainItem = item;
    plainItem.declared_ref = cs::SmartContractRef{};
    plainItem.declared_hash = cs::Hash{};
    EXPECT_EQ(plainItem.to_bytes().size(), item.to_bytes().size() - (sizeof(item.declared_ref.sequence) + sizeof(item.declared_ref.transaction) + item.declared_hash.size()));

    // an earlier build wrote no declaration: the same bytes without the tail
    auto old = item.to_bytes();
    old.resize(old.size() - (sizeof(item.declared_ref.sequence) + sizeof(item.declared_ref.transaction) + item.declared_hash.size()));
    const auto earlier = cs::SmartContracts::StateItem::from_bytes(old);
    EXPECT_EQ(earlier.state, item.state);
    EXPECT_FALSE(earlier.declared_ref.is_valid());
    EXPECT_EQ(earlier.declared_hash, cs::Hash{});
}

TEST(SmartContract, StaleMeansNotTheDeclaredState) {
    cs::SmartContracts::StateItem item;
    item.state = "current";
    EXPECT_FALSE(cs::SmartContracts::is_stale(item));            // nothing declared: unknown, not stale
    item.declared_hash = cscrypto::calculateHash(reinterpret_cast<const cs::Byte*>(item.state.data()), item.state.size());
    EXPECT_FALSE(cs::SmartContracts::is_stale(item));            // holds the declared state
    item.state = "an earlier one";
    EXPECT_TRUE(cs::SmartContracts::is_stale(item));             // another state
    item.state.clear();
    EXPECT_TRUE(cs::SmartContracts::is_stale(item));             // no state at all
}

// step 3 of the contract state sync plan: a peer's state is taken only for the declared call and hash
TEST(SmartContract, StateReplyIsCheckedAgainstTheDeclaration) {
    using Check = cs::SmartContracts::StateReplyCheck;
    cs::SmartContracts::StateItem item;
    const std::string good = "state after 9.2";
    cs::SmartContractRef declared;
    declared.sequence = 9;
    declared.transaction = 2;
    cs::SmartContractRef older = declared, newer = declared;
    older.sequence = 8;
    newer.sequence = 10;

    EXPECT_EQ(cs::SmartContracts::verify_state_reply(declared, good, item), Check::Undeclared);
    item.declared_ref = declared;
    item.declared_hash = cscrypto::calculateHash(reinterpret_cast<const cs::Byte*>(good.data()), good.size());
    EXPECT_EQ(cs::SmartContracts::verify_state_reply(declared, good, item), Check::Accepted);
    EXPECT_EQ(cs::SmartContracts::verify_state_reply(declared, "a forged state", item), Check::HashMismatch);
    EXPECT_EQ(cs::SmartContracts::verify_state_reply(declared, "", item), Check::HashMismatch);
    EXPECT_EQ(cs::SmartContracts::verify_state_reply(older, good, item), Check::Older);
    EXPECT_EQ(cs::SmartContracts::verify_state_reply(newer, good, item), Check::Newer);
}

TEST(SmartContract, StateFetcherPacesAndBacksOff) {
    cs::StateFetcher f;
    std::vector<csdb::Address> contracts;
    for (uint8_t i = 1; i <= 20; ++i) {
        contracts.push_back(csdb::Address::from_public_key(cs::Bytes(32, i)));
        f.markStale(contracts.back());
    }
    EXPECT_TRUE(f.isWanted(contracts[0]));
    // at most 16 a round, the rest the next round
    EXPECT_EQ(f.due(100).size(), cs::StateFetcher::kRequestsPerRound);
    EXPECT_EQ(f.due(101).size(), 4u);
    EXPECT_TRUE(f.due(102).empty());
    // asked again after the backoff, which doubles with each attempt up to the cap
    EXPECT_EQ(f.due(100 + cs::StateFetcher::kBaseBackoff).size(), cs::StateFetcher::kRequestsPerRound);
    EXPECT_EQ(cs::StateFetcher::backoff(0), 50u);
    EXPECT_EQ(cs::StateFetcher::backoff(1), 100u);
    EXPECT_EQ(cs::StateFetcher::backoff(20), cs::StateFetcher::kMaxBackoff);
    // a verified state ends the requests, and a reply for a contract not asked for is not an answer
    f.done(contracts[0]);
    EXPECT_FALSE(f.isWanted(contracts[0]));
    EXPECT_FALSE(f.isWanted(csdb::Address::from_public_key(cs::Bytes(32, 99))));
    EXPECT_EQ(f.size(), 19u);
    f.reset();
    EXPECT_EQ(f.size(), 0u);
}
