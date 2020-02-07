#ifndef API_DIAG_HANDLER_HPP
#define API_DIAG_HANDLER_HPP

#include <API_DIAG.h>

class Node;

namespace api_diag {
    class APIDiagHandler : public API_DIAGNull {

    public:
        APIDiagHandler(Node& node);
        APIDiagHandler(const APIDiagHandler&) = delete;

        void GetTransaction(GetTransactionResponse& _return, const TransactionId& id) override;

    private:
        Node& node_;
    };
}

#endif  // API_DIAG_HANDLER_HPP
