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

    static Id getId(const cs::Bytes& bin_pack);
    //void parse(const cs::Bytes& bin_pack);

    /**
     * Sends a rejected transactions info as a map <reason, count>, where every reject reason is supplied with count of transactions
     *
     * @author  Alexander Avramenko
     * @date    21.11.2019
     *
     * @param [in,out]  node        The node service
     * @param           rejected    The byte array with reason to reject every transaction
     */

    static void sendReject(Node& node, const cs::Bytes& rejected);

    /**
     * Parse reject info byte packet as a map <reason, count>, where every reject reason is supplied with count of transactions
     *
     * @author  Alexander Avramenko
     * @date    21.11.2019
     *
     * @param   bin_pack    The byte array from network package, must be a result of sendReject() method on remote node
     *
     * @returns A std::map&lt;Reject::Reason,uint16_t&gt;
     */

    static std::map<Reject::Reason, uint16_t> parseReject(const cs::Bytes& bin_pack);

    /**
     * Sends a black list update info as composition of node key and operation (add to list or remove from list)
     *
     * @author  Alexander Avramenko
     * @date    21.11.2019
     *
     * @param [in,out]  node    The node service
     * @param           key     The key of black list item, if blacklist completely cleared must be a Zero::key
     * @param           added   True if added to list, otherwise false if removed from list
     */

    static void sendBlackListUpdate(Node& node, const cs::PublicKey& key, bool added);

    /**
     * Parse black list update
     *
     * @author  Alexander Avramenko
     * @date    21.11.2019
     *
     * @param           bin_pack    The byte array pack, must be a product of sendBlackListUpdate() call on remote node
     * @param [in,out]  key         The placeholder of parsed result, if black list was completely cleared on remote node, contains Zero::key
     *
     * @returns True if it succeeds, false if it fails.
     */

    static bool parseBlackListUpdate(const cs::Bytes& bin_pack, cs::PublicKey& key);
};

#endif // EVENTREPORT_HPP
