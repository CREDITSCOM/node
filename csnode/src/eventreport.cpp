#include <csnode/eventreport.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>

#include <lib/system/logger.hpp>
#include <csnode/configholder.hpp>
#include <solver/smartcontracts.hpp>

const char* log_prefix = "Event: ";

/*static*/
std::string Reject::to_string(Reason r) {
    switch (r) {
    case WrongSignature:
        return "WrongSignature";
    case InsufficientMaxFee:
        return "InsufficientMaxFee";
    case NegativeResult:
        return "NegativeResult";
    case SourceIsTarget:
        return "SourceIsTarget";
    case DisabledInnerID:
        return "DisabledInnerID";
    case DuplicatedInnerID:
        return "DuplicatedInnerID";
    case MalformedContractAddress:
        return "MalformedContractAddress";
    case MalformedTransaction:
        return "MalformedTransaction";
    case ContractClosed:
        return "ContractClosed";
    case NewStateOutOfFee:
        return "NewStateOutOfFee";
    case EmittedOutOfFee:
        return "EmittedOutOfFee";
    case CompleteReject:
        return "CompleteReject";
    case LimitExceeded:
        return "LimitExceeded";
    case AmountTooLow:
        return "AmountTooLow";
    case AlreadyDelegated:
        return "AlreadyDelegated";
    case IncorrectTarget:
        return "IncorrectTarget";
    case MalformedDelegation:
        return "MalformedDelegation";
    case IncorrectSum:
        return "IncorrectSum";
    default:
        break;
    }
    return "?";
}

/*static*/
std::string Running::to_string(Status s) {
    switch (s) {
    case Stop:
        return "STOP";
    case ReadBlocks:
        return "READ_BLOCKS";
    case Run:
        return "RUN";
    default:
        break;
    }
    return "?";
}

/*static*/
void EventReport::sendRejectTransactions(Node& node, const cs::Bytes& rejected) {
    const auto& config = cs::ConfigHolder::instance().config()->getEventsReportData();
    if (!config.on || !config.reject_transaction) {
        return;
    }

    std::map<Reject::Reason, uint16_t> resume;
    for (const auto r : rejected) {
        if (r != Reject::Reason::None) {
            resume[Reject::Reason(r)] += 1;
        }
    }
    if (!resume.empty()) {
        cs::Bytes bin_pack;
        cs::ODataStream stream(bin_pack);
        stream << Id::RejectTransactions << uint8_t(resume.size());
        for (const auto& item : resume) {
            stream << item.first << item.second;
        }
        node.reportEvent(bin_pack);
    }
}

/*static*/
void EventReport::sendRejectContractExecution(Node& node, const cs::SmartContractRef& ref, Reject::Reason reason) {
    const auto& config = cs::ConfigHolder::instance().config()->getEventsReportData();
    if (!config.on || !config.reject_contract_execution) {
        return;
    }

    cs::Bytes bin_pack;
    cs::ODataStream out(bin_pack);
    out << Id::RejectContractExecution << ref.sequence << ref.transaction << reason;
    node.reportEvent(bin_pack);
}

/*static*/
std::map<Reject::Reason, uint16_t> EventReport::parseReject(const cs::Bytes& bin_pack) {
    std::map<Reject::Reason, uint16_t> resume;
    if (bin_pack.empty()) {
        return resume;
    }
    cs::IDataStream stream(bin_pack.data(), bin_pack.size());
    Id id = Id::None;
    stream >> id;
    if (id == Id::RejectTransactions) {
        uint8_t size = 0;
        Reject::Reason r = Reject::Reason::None;
        uint16_t cnt = 0;
        stream >> size;
        for (uint8_t i = 0; i < size; ++i) {
            stream >> r >> cnt;
            if (cnt == 0) {
                break;
            }
            resume[r] = cnt;
        }
    }
    return resume;
}

/*static*/
bool EventReport::parseRejectContractExecution(const cs::Bytes& bin_pack, cs::SmartContractRef& ref, Reject::Reason& reason) {
    if (bin_pack.empty()) {
        return false;
    }
    cs::IDataStream stream(bin_pack.data(), bin_pack.size());
    Id id = Id::None;
    stream >> id;
    if (id == Id::RejectContractExecution) {
        stream >> ref.sequence >> ref.transaction >> reason;
        return stream.isEmpty() && stream.isValid();
    }
    return false;
}

/*static*/
EventReport::Id EventReport::getId(const cs::Bytes& bin_pack) {
    if (bin_pack.empty()) {
        return Id::None;
    }
    return (EventReport::Id) bin_pack.front();
}

// ignore count_rounds when erase gray list, in that case only 1 round minimum is supposed
/*static*/
void EventReport::sendGrayListUpdate(Node& node, const cs::PublicKey& key, bool added, uint32_t count_rounds /*= 1*/) {
    const auto& config = cs::ConfigHolder::instance().config()->getEventsReportData();
    if (!config.on) {
        return;
    }
    if (added) {
        if (!config.add_to_gray_list) {
            return;
        }
    }
    else {
        if (!config.erase_from_gray_list) {
            return;
        }
    }
    EventReport::sendListUpdate(node, key, added, count_rounds);
}

/*static*/
void EventReport::sendBlackListUpdate(Node& node, const cs::PublicKey& key, bool added) {
    // black list events are always sent
    EventReport::sendListUpdate(node, key, added, 0);
}

/*private static*/
void EventReport::sendListUpdate(Node& node, const cs::PublicKey& key, bool added, uint32_t count_rounds) {
    constexpr size_t len = sizeof(EventReport::Id) + cscrypto::kPublicKeySize + sizeof(count_rounds);
    cs::Bytes bin_pack;
    bin_pack.reserve(len);
    cs::ODataStream stream(bin_pack);
    stream << (added ? Id::AddToList : Id::EraseFromList) << key;
    // send zero value is senseless
    if (count_rounds > 0) {
        stream << count_rounds;
    }
    node.reportEvent(bin_pack);
}

/*static*/
bool EventReport::parseListUpdate(const cs::Bytes& bin_pack, cs::PublicKey& key, uint32_t& counter, bool& is_black) {
    if (bin_pack.empty()) {
        return false;
    }
    cs::IDataStream stream(bin_pack.data(), bin_pack.size());
    Id id = Id::None;
    stream >> id;
    if (id == Id::AddToList || id == Id::EraseFromList) {
        if (stream.isAvailable(key.size())) {
            stream >> key;
            // only nonzero value is sent
            if (stream.isAvailable(sizeof(uint32_t))) {
                stream >> counter;
            }
            else {
                counter = 0;
            }
            is_black = (counter == 0);
            return stream.isValid() && stream.isEmpty();
        }
    }
    return false;
}

/*static*/
void EventReport::sendInvalidBlockAlarm(Node& node, const cs::PublicKey& source, cs::Sequence sequence) {
    const auto& config = cs::ConfigHolder::instance().config()->getEventsReportData();
    if (!config.on || !config.alarm_invalid_block) {
        return;
    }
    constexpr size_t len = sizeof(EventReport::Id) + cscrypto::kPublicKeySize + sizeof(sequence);
    cs::Bytes bin_pack(len);
    cs::ODataStream out(bin_pack);
    out << Id::AlarmInvalidBlock << source << sequence;
    node.reportEvent(bin_pack);
}

/*static*/
bool EventReport::parseInvalidBlockAlarm(const cs::Bytes& bin_pack, cs::PublicKey& source, cs::Sequence& sequence) {
    if (bin_pack.empty()) {
        return false;
    }
    cs::IDataStream in(bin_pack.data(), bin_pack.size());
    Id id = Id::None;
    in >> id;
    if (id != Id::AlarmInvalidBlock) {
        return false;
    }
    in >> source >> sequence;
    return in.isValid() && in.isEmpty();
}

/*static*/
void EventReport::sendConsensusProblem(Node& node, Id problem_id, const cs::PublicKey& problem_source) {
    const auto& config = cs::ConfigHolder::instance().config()->getEventsReportData();
    if (!config.on) {
        return;
    }
    switch (problem_id) {
    case Id::ConsensusFailed:
        if (!config.consensus_failed) {
            return;
        }
        break;
    case Id::ConsensusLiar:
        if (!config.consensus_liar) {
            return;
        }
        break;
    case Id::ConsensusSilent:
        if (!config.consensus_silent) {
            return;
        }
        break;
    default:
        cswarning() << log_prefix << "incompatible consensus report problem id=" << uint32_t(problem_id);
        return;
    }
    constexpr size_t len = sizeof(EventReport::Id) + cscrypto::kPublicKeySize;
    cs::Bytes bin_pack(len);
    cs::ODataStream out(bin_pack);
    out << problem_id << problem_source;
    node.reportEvent(bin_pack);
}

/*static*/
EventReport::Id EventReport::parseConsensusProblem(const cs::Bytes& bin_pack, cs::PublicKey& problem_source) {
    if (bin_pack.empty()) {
        return Id::None;
    }
    cs::IDataStream in(bin_pack.data(), bin_pack.size());
    Id id = Id::None;
    in >> id;
    switch(id) {
    case Id::ConsensusFailed:
    case Id::ConsensusLiar:
    case Id::ConsensusSilent:
        in >> problem_source;
        if (in.isValid() && in.isEmpty()) {
            return id;
        }
        break;
    default:
        break;
    }
    return Id::None;
}

/*static*/
void EventReport::sendContractsProblem(Node& node, Id problem_id, const cs::PublicKey& problem_source, const ContractConsensusId& consensus_id) {
    const auto& config = cs::ConfigHolder::instance().config()->getEventsReportData();
    if (!config.on) {
        return;
    }
    switch (problem_id) {
    case Id::ContractsFailed:
        if (!config.contracts_failed) {
            return;
        }
        break;
    case Id::ContractsLiar:
        if (!config.contracts_liar) {
            return;
        }
        break;
    case Id::ContractsSilent:
        if (!config.contracts_silent) {
            return;
        }
        break;
    default:
        cswarning() << log_prefix << "incompatible contract report problem id=" << uint32_t(problem_id);
        return;
    }
    constexpr size_t len = sizeof(EventReport::Id) + cscrypto::kPublicKeySize
        + sizeof(consensus_id.round) + sizeof(consensus_id.transaction) + sizeof(consensus_id.iteration);
    cs::Bytes bin_pack(len);
    cs::ODataStream out(bin_pack);
    out << problem_id << problem_source << consensus_id.round << consensus_id.transaction << consensus_id.iteration;
    node.reportEvent(bin_pack);
}

/*static*/
EventReport::Id EventReport::parseContractsProblem(const cs::Bytes& bin_pack, cs::PublicKey& problem_source, ContractConsensusId& consensus_id) {
    if (bin_pack.empty()) {
        return Id::None;
    }
    cs::IDataStream in(bin_pack.data(), bin_pack.size());
    Id id = Id::None;
    in >> id;
    switch (id) {
    case Id::ContractsFailed:
    case Id::ContractsLiar:
    case Id::ContractsSilent:
        in >> problem_source >> consensus_id.round >> consensus_id.transaction >> consensus_id.iteration;
        if (in.isValid() && in.isEmpty()) {
            return id;
        }
        break;
    default:
        break;
    }
    return Id::None;
}

/*static*/ void EventReport::sendRunningStatus(Node& node, Running::Status status) {
    constexpr size_t len = sizeof(EventReport::Id) + sizeof(Running::Status);
    cs::Bytes bin_pack(len);
    cs::ODataStream out(bin_pack);
    out << Id::RunningStatus << status;
    node.reportEvent(bin_pack);
}

/*static*/
bool EventReport::parseRunningStatus(const cs::Bytes& bin_pack, Running::Status& status) {
    if (bin_pack.empty()) {
        return false;
    }
    cs::IDataStream in(bin_pack.data(), bin_pack.size());
    Id id = Id::None;
    in >> id;
    if (id != Id::RunningStatus) {
        return false;
    }
    in >> status;
    return in.isValid() && in.isEmpty();
}
