#define TESTING

#include <solver/smartcontracts.hpp>
#include <csdb/transaction.hpp>
#include "cscrypto/cryptotypes.hpp"
#include "cscrypto/memoryprotection.hpp"
#include <transactionsvalidator.hpp>

#include <vector>
#include "gtest/gtest.h"


TEST(TransactionsValidator, multi) {

    using namespace cs;
    using PrivKey = cscrypto::ByteArray<64>;
    cs::Signature sig{};

    cscrypto::Bytes bytes;
    DecodeBase58("CzvqNvaQpsE2v4FFTJ2mnTKu48JsMhLzzMALja8bt7DD", bytes);
    csdb::Address src = csdb::Address::from_public_key(bytes);
    DecodeBase58("G2GeLfwjg6XuvoWnZ7ssx9EPkEBqbYL3mw3fusgpzoBk", bytes);
    csdb::Address tgt = csdb::Address::from_public_key(bytes);
    PrivKey privKey;
    DecodeBase58("4h6D57iTFE7883ZMMee7rW7LgLm6G46Jz4YVzrp91LfSxQGFxQnNF5QHqnrn4Q9KKVyXfRQJrCiM7rVtNHMPCcwM", bytes);
    ASSERT_TRUE(bytes.size() == 64);
    // basic transaction
    csdb::Transaction t(1LL, src, tgt, csdb::Currency{1}, csdb::Amount(1), csdb::AmountCommission(1.0), csdb::AmountCommission(0.0), sig);
    Reject::Reason r = Reject::Reason::None;
    WalletsState::WalletData wallState;
    wallState.balance_ = csdb::Amount{ 0 };
    wallState.lastTrxInd_ = 0L;


    if (!wallState.trxTail_.isAllowed(t.innerID())) {
        r = wallState.trxTail_.isDuplicated(t.innerID()) ? Reject::Reason::DuplicatedInnerID : Reject::Reason::DisabledInnerID;
        ASSERT_FALSE(r == Reject::Reason::DuplicatedInnerID);
        ASSERT_FALSE(r == Reject::Reason::DisabledInnerID);
    }
    if (SmartContracts::is_executable(t)) {

    }
}