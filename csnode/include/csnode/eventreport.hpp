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
     * Sends a gray and black list update info as composition of node key and operation (add to list
     * or remove from list).
     *
     * @author  Alexander Avramenko
     * @date    21.11.2019
     *
     * @param [in,out]  node            The node service.
     * @param           key             The key of gray or black list item, if list is completely
     *  cleared must be a Zero::key.
     * @param           added           True if added to list, otherwise false if removed from list.
     * @param           count_rounds    (Optional) The count rounds to be in list. 0 in case of clear
     *  list or add to black list.
     */

    static void sendGrayListUpdate(Node& node, const cs::PublicKey& key, bool added, uint32_t count_rounds = 0);

    /**
     * Parse black list update
     *
     * @author  Alexander Avramenko
     * @date    21.11.2019
     *
     * @param           bin_pack    The byte array pack, must be a product of sendGrayListUpdate()
     *  call on remote node.
     * @param [in,out]  key         The placeholder for parsed result, if black list was completely
     *  cleared on remote node, contains Zero::key.
     * @param [in,out]  counter     The placeholder for counter of rounds to be in gray list, being
     *  in black list is permanent, so contains 0 after method returns.
     *
     * @returns True if it succeeds, false if it fails.
     */

    static bool parseGrayListUpdate(const cs::Bytes& bin_pack, cs::PublicKey& key, uint32_t& counter);

    /**
     * Sends an invalid block alarm report
     *
     * @author  Alexander Avramenko
     * @date    28.11.2019
     *
     * @param [in,out]  node        The node.
     * @param           sequence    The sequence of invalid block
     * @param           source      Source node of the invalid block
     */

    static void sendInvalidBlockAlarm(Node& node, const cs::PublicKey& source, cs::Sequence sequence);

    /**
     * Parse invalid block alarm report
     *
     * @author  Alexander Avramenko
     * @date    28.11.2019
     *
     * @param           bin_pack    he byte array pack, must be a product of sendInvalidBlockAlarm()
     *  call on remote node
     * @param [in,out]  source      placeholder for the key of the source node of the invalid block
     * @param [in,out]  sequence    placeholder for the sequence of invalid block
     *
     * @returns True if it succeeds, false if it fails. If OK both placeholders contains data
     */

    static bool parseInvalidBlockAlarm(const cs::Bytes& bin_pack, cs::PublicKey& source, cs::Sequence& sequence);

    static inline void sendConsensusSilent(Node& node, const cs::PublicKey& problem_source) {
        sendConsensusProblem(node, Id::ConsensusSilent, problem_source);
    }

    static inline void sendConsensusLisr(Node& node, const cs::PublicKey& problem_source) {
        sendConsensusProblem(node, Id::ConsensusLiar, problem_source);
    }

    static inline void sendConsensusFailed(Node& node, const cs::PublicKey& problem_source) {
        sendConsensusProblem(node, Id::ConsensusFailed, problem_source);
    }

    static inline void sendContractsSilent(Node& node, const cs::PublicKey& problem_source) {
        sendConsensusProblem(node, Id::ContractsSilent, problem_source);
    }

    static inline void sendContractsLisr(Node& node, const cs::PublicKey& problem_source) {
        sendConsensusProblem(node, Id::ContractsLiar, problem_source);
    }

    static inline void sendContractsFailed(Node& node, const cs::PublicKey& problem_source) {
        sendConsensusProblem(node, Id::ContractsFailed, problem_source);
    }

    /**
     * Parse consensus problem data, 
     *
     * @author  Alexander Avramenko
     * @date    03.12.2019
     *
     * @param           bin_pack        The byte array pack, must be a product of sendConsensusProblem()
     *  call on remote node.
     * @param [in,out]  problem_source  The placeholder for the problem source key.
     *
     * @returns A problem Id or Id::None if parse failed
     */

    static Id parseConsensusProblem(const cs::Bytes& bin_pack, cs::PublicKey& problem_source);

private:

    static void sendConsensusProblem(Node& node, Id problem_id, const cs::PublicKey& problem_source);

};

#endif // EVENTREPORT_HPP
