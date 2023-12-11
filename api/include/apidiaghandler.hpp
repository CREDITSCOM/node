#ifndef API_DIAG_HANDLER_HPP
#define API_DIAG_HANDLER_HPP

#include <API_DIAG.h>

class Node;

namespace api_diag {
    class APIDiagHandler : public API_DIAGNull {

    public:
        APIDiagHandler(Node& node);
        APIDiagHandler(const APIDiagHandler&) = delete;

        // former start node proto
        void GetActiveNodes(ActiveNodesResult& _return) override;
        void GetActiveTrustNodes(ActiveTrustNodesResult& _return) override;
        void GetActiveTransactionsCount(ActiveTransactionsResult& _return) override;
        void GetNodeStat(ActiveTrustNodesResult& _return, const general::Address& address) override;
        void GetNodeRewardEvaluation(RewardEvaluation& _return, const general::Address& address);
        // diagnostic proto
        void GetTransaction(GetTransactionResponse& _return, const TransactionId& id) override;

        void GetNodeInfo(NodeInfoRespone& _return, const NodeInfoRequest& request) override;

        void GetSupply(SupplyInfo& _return) override;

        void SetRawData(general::APIResponse& _return, const std::string& data) override;

        void UserCommand(general::APIResponse& _return, const std::string& data) override;

    private:
        Node& node_;
    };

    // see: deprecated transport.cpp #1008
    constexpr int8_t platform() {
#ifdef _WIN32
        return api_diag::Platform::OS_Windows;
#elif __APPLE__
        return api_diag::Platform::OS_MacOS;
#else
        return api_diag::Platform::OS_Linux;
#endif
    }

}

#endif  // API_DIAG_HANDLER_HPP
