#include <apidiaghandler.hpp>

#include <node.hpp>

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
            data.__set_signature(to_string<cs::Signature>(t.signature()));

            _return.__set_transaction(data);
        }
        _return.__set_status(resp);
    }

}

