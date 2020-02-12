#include <apidiaghandler.hpp>

#include <node.hpp>
#include <smartcontracts.hpp>
#include <serializer.hpp>
#include <tokens.hpp>
#include <nodecore.hpp>

#include <api_types.h>

// see: apihandler.cpp #175
//extern std::string fromByteArray(const cs::PublicKey& key);
template <typename TArr>
std::string to_string(const TArr& ar) {
    return std::string(ar.begin(), ar.end());
}

namespace api_diag {

    // APIResponse::code values:
    // 0 for success, 1 for failure, 2 for not being implemented (currently unused)
    const int8_t kOk = 0;
    const int8_t kError = 1;
    const int8_t kNotImplemented = 2;    

    APIDiagHandler::APIDiagHandler(Node& node)
        : node_(node)
    {}

    void APIDiagHandler::GetActiveNodes(ActiveNodesResult& _return) {
        general::APIResponse resp;
        std::vector<cs::PeerData> peers;
        if (!node_.getKnownPeers(peers)) {
            resp.__set_code(kError);
            resp.__set_message("Discovery service is unavailable on this node");
            for (const auto& peer: peers) {
                api_diag::ServerNode node;
                node.__set_ip(peer.ip);
                node.__set_port(std::to_string(peer.port));
                node.__set_publicKey(peer.id);
                node.__set_version(std::to_string(peer.version));
                node.__set_platform(std::to_string(peer.platform));
                node.__set_countTrust(0);
                //node.__set_hash("");
                node.__set_timeActive(0);
                node.__set_timeRegistration(0);
            }
        }
        else {
            resp.__set_code(kOk);
        }
        _return.__set_result(resp);
    }

    void APIDiagHandler::GetActiveTransactionsCount(ActiveTransactionsResult& _return) {
        general::APIResponse resp;
        resp.__set_code(kOk);
        _return.__set_result(resp);
        _return.__set_count(std::to_string(node_.getTotalTransactionsCount()));
    }


    void APIDiagHandler::GetTransaction(GetTransactionResponse& _return, const TransactionId& id) {

        const auto t = node_.getBlockChain().loadTransaction(csdb::TransactionID(id.sequence, id.index));
        
        general::APIResponse resp;
        if (!t.is_valid()) {
            resp.__set_code(kError); // failed
            resp.__set_message("unable to load rewuested transaction");
        }
        else {
            resp.code = kOk;

            ::api_diag::TransactionType tt = TT_Transfer;
            ::api_diag::TransactionData data;
            data.__set_id(t.innerID());
            data.__set_source(to_string<cs::PublicKey>(t.source().public_key()));
            data.__set_target(to_string<cs::PublicKey>(t.target().public_key()));
            // sum
            const auto sum = t.amount();
            ::general::Amount amount;
            amount.__set_integral(sum.integral());
            amount.__set_fraction(sum.fraction());
            ::api_diag::Money money;
            money.__set_amount(amount);
            money.__set_value(sum.to_double());
            data.__set_sum(money);
            ::api_diag::AmountCommission fee;
            // max fee
            fee.__set_bits((int16_t)t.max_fee().get_raw());
            fee.__set_value(t.max_fee().to_double());
            data.__set_max_fee(fee); 
            // actual fee
            fee.__set_bits((int16_t)t.counted_fee().get_raw());
            fee.__set_value(t.counted_fee().to_double());
            data.__set_actual_fee(fee);
            // signature
            data.__set_signature(to_string<cs::Signature>(t.signature()));
            // time
            data.__set_timestamp((int64_t)t.get_time());

            // user fields & contracts
            if (cs::SmartContracts::is_executable(t)) {
                // cs::trx_uf::start::Methods == cs::trx_uf::deploy::Code
                std::string bytes = t.user_field(cs::trx_uf::deploy::Code).value<std::string>();
                if (!bytes.empty()) {

                    ::api_diag::Contract contract;
                    
                    ::api::SmartContractInvocation tmp = cs::Serializer::deserialize<::api::SmartContractInvocation>(std::move(bytes));
                    if (cs::SmartContracts::is_deploy(t)) {

                        tt = TT_ContractDeploy;
                        
                        if (tmp.__isset.smartContractDeploy) {
                            const auto& src = tmp.smartContractDeploy;
                            ::api_diag::ContractDeploy deploy;
                            deploy.__set_byteCodeObjects(src.byteCodeObjects);
                            deploy.__set_sourceCode(src.sourceCode);
                            deploy.__set_tokenStandard(src.tokenStandard);
                            contract.__set_deploy(deploy);

                            if (src.tokenStandard != NotAToken) {
                                tt = TT_TokenDeploy;
                            }
                        }
                        else {
                            // deploy, but not set properly
                            tt = TT_Malformed;
                        }
                    }
                    else {
                        // cs::SmartContracts::is_execute(t)
                        // 
                        tt = TT_ContractCall;

                        //TODO:  test token transfer
                        
                        ::api_diag::ContractCall invocation;
                        invocation.__set_getter(tmp.forgetNewState);
                        invocation.__set_method(tmp.method);
                        invocation.__set_params(tmp.params);
                        invocation.__set_uses(tmp.usedContracts);
                        contract.__set_call(invocation);
                    }

                    data.__set_contract(contract);
                }
                else {
                    // string not set
                    tt = TT_Malformed;
                }

            }
            else if (cs::SmartContracts::is_new_state(t)) {

                tt = TT_ContractState;

                ::api_diag::ContractState state;

                using namespace cs::trx_uf::new_state;

                csdb::UserField fld = t.user_field(Value);
                if (fld.is_valid()) {
                    state.__set_hashed(false);
                    state.__set_content(fld.value<std::string>());
                }

                fld = t.user_field(Hash);
                if (fld.is_valid()) {
                    state.__set_hashed(true);
                    state.__set_content(fld.value<std::string>());
                }

                fld = t.user_field(RetVal);
                if (fld.is_valid()) {
                    state.__set_returned(fld.value<std::string>());
                }

                fld = t.user_field(Fee);
                if (fld.is_valid()) {
                    csdb::Amount tmp = fld.value<csdb::Amount>();
                    amount.__set_integral(tmp.integral());
                    amount.__set_fraction(tmp.fraction());
                    money.__set_amount(amount);
                    money.__set_value(tmp.to_double());
                    state.__set_fee(money);
                }

                fld = t.user_field(RefStart);
                if (fld.is_valid()) {
                    cs::SmartContractRef ref(fld);
                    ::api_diag::TransactionId call;
                    call.__set_sequence((int64_t)ref.sequence);
                    call.__set_index((int16_t)ref.transaction);
                    state.__set_call(call);
                }

                ::api_diag::Contract contract;
                contract.__set_state(state);
                data.__set_contract(contract);
            }
            else {

                std::vector<api_diag::UserField> user_fields;

                for (auto fid : t.user_field_ids()) {
                    api_diag::UserField user_field;
                    user_field.__set_id((int8_t)fid);
                    const auto& fld = t.user_field(fid);
                    if (fld.is_valid()) {
                        bool is_valid_data = true;
                        UserFielData fld_data;

                        switch (fld.type()) {
                        case csdb::UserField::Type::Amount:
                            {
                                const csdb::Amount v = fld.value<csdb::Amount>();
                                amount.__set_integral(v.integral());
                                amount.__set_fraction(v.fraction());
                                fld_data.__set_amount(amount);
                            }
                            break;
                        case csdb::UserField::Type::Integer:
                            {
                                int64_t v = fld.value<int64_t>();
                                fld_data.__set_integer(v);
                                using namespace cs::trx_uf::sp;
                                if (fid == delegated) {
                                    if (v == de::legate) {
                                        tt = TT_Delegation;
                                    }
                                    else if (v == de::legated_withdraw) {
                                        tt = TT_RevokeDelegation;
                                    }
                                }
                            }
                            break;
                        case csdb::UserField::Type::String:
                            fld_data.__set_bytes(fld.value<std::string>());
                            break;
                        default:
                            is_valid_data = false;
                            break;
                        }
                        
                        if (is_valid_data) {
                            user_field.__set_data(fld_data);
                        }

                    }

                    user_fields.push_back(user_field);
                }

                if (!user_fields.empty()) {
                    data.__set_userFields(user_fields);
                }

            }

            // transaction type
            if (tt == TT_Transfer) {
                if (node_.getSolver()->smart_contracts().is_payable_call(t)) {
                    tt = TT_ContractReplenish;
                }
            }
            data.__set_type(tt);
            _return.__set_transaction(data);
        }
        _return.__set_status(resp);
    }

}

