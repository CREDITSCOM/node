#include <apidiaghandler.hpp>

#include <node.hpp>
#include <smartcontracts.hpp>
#include <serializer.hpp>

#include <api_types.h>

// see: apihandler.cpp #175
//extern std::string fromByteArray(const cs::PublicKey& key);
template <typename TArr>
std::string to_string(const TArr& ar) {
    std::string res;
    {
        res.reserve(ar.size());
        std::transform(ar.begin(), ar.end(), std::back_inserter<std::string>(res), [](uint8_t _) { return char(_); });
    }
    return res;
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

    void APIDiagHandler::GetTransaction(GetTransactionResponse& _return, const TransactionId& id) {

        const auto t = node_.getBlockChain().loadTransaction(csdb::TransactionID(id.sequence, id.index));
        
        general::APIResponse resp;
        if (!t.is_valid()) {
            resp.__set_code(1); // failed
            resp.__set_message("unable to load rewuested transaction");
        }
        else {
            resp.code = kOk;

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

            // user fields
            std::vector<api_diag::UserField> user_fields;
            for (auto fid : t.user_field_ids()) {
                api_diag::UserField user_field;
                user_field.__set_id((int8_t)fid);
                const auto& fld = t.user_field(fid);
                if (fld.is_valid()) {
                    bool is_valid_data = true;
                    UserFielData fld_data;
                    if (cs::SmartContracts::is_executable(t)) {
                        std::string bytes = fld.value<std::string>();
                        if (!bytes.empty()) {
                            ::api::SmartContractInvocation tmp = cs::Serializer::deserialize<::api::SmartContractInvocation>(std::move(bytes));
                            if (cs::SmartContracts::is_deploy(t)) {
                                if (tmp.__isset.smartContractDeploy) {
                                    const auto& src = tmp.smartContractDeploy;
                                    ::api_diag::ContractDeploy deploy;
                                    deploy.__set_byteCodeObjects(src.byteCodeObjects);
                                    deploy.__set_hashState(src.hashState);
                                    deploy.__set_sourceCode(src.sourceCode);
                                    deploy.__set_tokenStandard(src.tokenStandard);
                                    fld_data.__set_deploy(deploy);
                                }
                                else {
                                    // deploy, but not set properly
                                    is_valid_data = false;
                                }
                            }
                            else {
                                // cs::SmartContracts::is_execute(t)
                                ::api_diag::ContractCall invocation;
                                invocation.__set_getter(tmp.forgetNewState);
                                invocation.__set_method(tmp.method);
                                invocation.__set_params(tmp.params);
                                invocation.__set_version(tmp.version);
                                invocation.__set_uses(tmp.usedContracts);
                                fld_data.__set_call(invocation);
                            }

                        }
                        else {
                            // string not set
                            is_valid_data = false;
                        }
                    }
                    else {
                        // other, non-executable
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
                            fld_data.__set_integer(fld.value<int64_t>());
                            break;
                        case csdb::UserField::Type::String:
                            fld_data.__set_bytes(fld.value<std::string>());
                            break;
                        default:
                            is_valid_data = false;
                            break;
                        }
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
            _return.__set_transaction(data);
        }
        _return.__set_status(resp);
    }

}

