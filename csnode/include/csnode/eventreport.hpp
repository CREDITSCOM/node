#if !defined(EVENTREPORT_HPP)
#define EVENTREPORT_HPP

#include <lib/system/common.hpp>

#include <cstdint>
#include <vector>
#include <map>

class Node;

namespace cs
{
    struct SmartContractRef;
}

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
        CompleteReject,

        LimitExceeded
    };

    static std::string to_string(Reason r);
};

struct Running {
    enum Status : uint8_t {
        Stop = 0,
        ReadBlocks = 1, // is not supproted yet
        Run = 2
    };

    static std::string to_string(Status s);
};

struct ContractConsensusId {
    cs::RoundNumber round;
    uint32_t transaction;
    uint32_t iteration;
};

class EventReport {
public:
    enum Id : uint8_t {
        None = 0,
        AlarmInvalidBlock,
        Bootstrap,
        ConsensusLiar,
        ConsensusSilent,
        ConsensusFailed,
        ContractsLiar,
        ContractsSilent,
        ContractsFailed,
        AddToList,
        EraseFromList,
        RejectTransactions,
        RejectContractExecution,
        RunningStatus
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

    static void sendRejectTransactions(Node& node, const cs::Bytes& rejected);

    static void sendRejectContractExecution(Node& node, const cs::SmartContractRef& ref, Reject::Reason reason);

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

    static bool parseRejectContractExecution(const cs::Bytes& bin_pack, cs::SmartContractRef& ref, Reject::Reason& reason);

    /**
     * Sends a gray list update info as composition of node key and operation (add to list
     * or remove from list).
     *
     * @author  Alexander Avramenko
     * @date    21.11.2019
     *
     * @param [in,out]  node            The node service.
     * @param           key             The key of gray or black list item, if list is completely
     *  cleared must be a Zero::key.
     * @param           added           True if added to list, otherwise false if removed from list.
     * @param           count_rounds    The count rounds to be in list. 1 in case of clear, only 1 round is guaranteed
     *  list or add to black list.
     */

    static void sendGrayListUpdate(Node& node, const cs::PublicKey& key, bool added, uint32_t count_rounds = 1);

    /**
     * Sends black list update info as composition of node key and operation (add to list
     * or remove from list).
     *
     * @author  Alexander Avramenko
     * @date    21.11.2019
     *
     * @param [in,out]  node            The node service.
     * @param           key             The key of gray or black list item, if list is completely
     *  cleared must be a Zero::key.
     * @param           added           True if added to list, otherwise false if removed from list.
     *  list or add to black list.
     */

    static void sendBlackListUpdate(Node& node, const cs::PublicKey& key, bool added);

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
     * @param [in,out]  is_black    True if target list is black, false if it is gray.
     *
     * @returns True if it succeeds, false if it fails.
     */

    static bool parseListUpdate(const cs::Bytes& bin_pack, cs::PublicKey& key, uint32_t& counter, bool& is_black);

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

    static inline void sendConsensusLiar(Node& node, const cs::PublicKey& problem_source) {
        sendConsensusProblem(node, Id::ConsensusLiar, problem_source);
    }

    static inline void sendConsensusFailed(Node& node, const cs::PublicKey& problem_source) {
        sendConsensusProblem(node, Id::ConsensusFailed, problem_source);
    }

    static inline void sendContractsSilent(Node& node, const cs::PublicKey& problem_source, const ContractConsensusId& consensus_id) {
        sendContractsProblem(node, Id::ContractsSilent, problem_source, consensus_id);
    }

    static inline void sendContractsLiar(Node& node, const cs::PublicKey& problem_source, const ContractConsensusId& consensus_id) {
        sendContractsProblem(node, Id::ContractsLiar, problem_source, consensus_id);
    }

    static inline void sendContractsFailed(Node& node, const cs::PublicKey& problem_source, const ContractConsensusId& consensus_id) {
        sendContractsProblem(node, Id::ContractsFailed, problem_source, consensus_id);
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
     * @returns A problem Id or Id::None if parse failed.
     */

    static Id parseConsensusProblem(const cs::Bytes& bin_pack, cs::PublicKey& problem_source);

    /**
     * Parse contracts problem
     *
     * @author  Alexander Avramenko
     * @date    03.12.2019
     *
     * @param           bin_pack        The byte array pack, must be a product of sendContractsProblem()
     * @param [in,out]  problem_source  The placeholder for the problem source.
     * @param [in,out]  consensus_id    The placeholder for the identifier of consensus.
     *
     * @returns An ID.
     */

    static Id parseContractsProblem(const cs::Bytes& bin_pack, cs::PublicKey& problem_source, ContractConsensusId& consensus_id);

    /**
     * Sends a running status
     *
     * @author  Alexander Avramenko
     * @date    19.12.2019
     *
     * @param [in,out]  node    The node service.
     * @param           status  The new status of node.
     */

    static void sendRunningStatus(Node& node, Running::Status status);

    /**
     * Parse running status
     *
     * @author  Alexander Avramenko
     * @date    19.12.2019
     *
     * @param           bin_pack    The bin pack, must be a product of sendRunningStatus()
     * @param [in,out]  status      The placeholder for the new status value.
     *
     * @returns True if it succeeds, false if it fails.
     */

    static bool parseRunningStatus(const cs::Bytes& bin_pack, Running::Status& status);

private:

    static void sendListUpdate(Node& node, const cs::PublicKey& key, bool added, uint32_t count_rounds);
    static void sendConsensusProblem(Node& node, Id problem_id, const cs::PublicKey& problem_source);
    static void sendContractsProblem(Node& node, Id problem_id, const cs::PublicKey& problem_source, const ContractConsensusId& consensus_id);

};

#endif // EVENTREPORT_HPP
