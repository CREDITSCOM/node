#define TESTING

#include <solver/smartcontracts.hpp>
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
