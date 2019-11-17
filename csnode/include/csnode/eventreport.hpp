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

    static Id getId(const cs::Bytes& bin_pack);
    //void parse(const cs::Bytes& bin_pack);

    static void sendReject(Node& node, const cs::Bytes& rejected);
    static std::map<Reject::Reason, uint16_t> parseReject(const cs::Bytes& bin_pack);
};

#endif // EVENTREPORT_HPP
