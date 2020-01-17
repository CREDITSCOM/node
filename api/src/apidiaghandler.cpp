#include <apidiaghandler.hpp>

#include <node.hpp>

namespace api_diag {

    APIDiagHandler::APIDiagHandler(Node& node)
        : node_(node)
    {}

    void APIDiagHandler::GetTransaction(GetTransactionResponse& _return, const TransactionId& /*id*/) {
        general::APIResponse resp;
        resp.__set_code(2); // not implemented yet
        resp.__set_message("not implemented yet");
        _return.__set_status(resp);
    }

}

