#if !defined(EVENTREPORT_HPP)
#define EVENTREPORT_HPP

#include <lib/system/common.hpp>

#include <cstdint>
#include <vector>
#include <map>

class Node;

struct Reject {
    enum Reason : uint8_t {
        None = 0,

        // general reasons, both actual for CS and contract transactions:
        
        WrongSignature,
        InsufficientMaxFee,
        NegativeResult,
        SourceIsTarget,
        DisabledInnerID,
        DuplicatedInnerID,

        // contract related reasons:
        
        MalformedContractAddress,
        MalformedTransaction,
        ContractClosed,
        NewStateOutOfFee,
        EmittedOutOfFee,
        CompleteReject
    };

    static std::string to_string(Reason r);
};

class EventReport {
public:
    enum Id : uint8_t {
        None = 0,
        AlarmInvalidBlock,
        BigBang,
        ConsensusLiar,
        ConsensusSilent,
        ConsensusFailed,
        ContractsLiar,
        ContractsSilent,
        ContractsFailed,
        AddGrayList,
        EraseGrayList,
        RejectTransactions,
        RejectContractExecution,
        RejectContractConsensus
    };

    constexpr static char* log_prefix = "Event: ";

    EventReport(Node& node)
        : node_(node) {
    }

    static Id getId(const cs::Bytes& bin_pack);
    //void parse(const cs::Bytes& bin_pack);

    void sendReject(const std::vector<Reject::Reason>& rejected);
    static std::map<Reject::Reason, uint16_t> parseReject(const cs::Bytes& bin_pack);

private:    

    Node& node_;
};

#endif // EVENTREPORT_HPP
