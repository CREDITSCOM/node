#include <csnode/eventreport.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>

#include <lib/system/logger.hpp>

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
    default:
        break;
    }
    return "?";
}

/*static*/
void EventReport::sendReject(Node& node, const cs::Bytes& rejected) {
    std::map<Reject::Reason, uint16_t> resume;
    for (const auto r : rejected) {
        if (r != Reject::Reason::None) {
            resume[Reject::Reason(r)] += 1;
        }
    }
    if (!resume.empty()) {
        cs::Bytes bin_pack;
        cs::DataStream stream(bin_pack);
        stream << Id::RejectTransactions << uint8_t(resume.size());
        for (const auto& item : resume) {
            stream << item.first << item.second;
        }
        node.reportEvent(bin_pack);
    }
}

/*static*/
std::map<Reject::Reason, uint16_t> EventReport::parseReject(const cs::Bytes& bin_pack) {
    std::map<Reject::Reason, uint16_t> resume;
    if (bin_pack.empty()) {
        return resume;
    }
    cs::DataStream stream(bin_pack.data(), bin_pack.size());
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
EventReport::Id EventReport::getId(const cs::Bytes& bin_pack) {
    if (bin_pack.empty()) {
        return Id::None;
    }
    return (EventReport::Id) bin_pack.front();
}

//void EventReport::parse(const cs::Bytes& bin_pack) {
//    const Id id = getId(bin_pack);
//    if (id == Id::RejectTransactions) {
//        auto resume = parseReject(bin_pack);
//        if (!resume.empty()) {
//            csdebug() << 
//        }
//    }
//}

// count_rounds is optional, 0 in case of clear list or add to black list.
/*static*/
void EventReport::sendGrayListUpdate(Node& node, const cs::PublicKey& key, bool added, uint32_t count_rounds /*= 0*/) {
    constexpr size_t len = sizeof(EventReport::Id) + cscrypto::kPublicKeySize + sizeof(count_rounds);
    cs::Bytes bin_pack;
    bin_pack.reserve(len);
    cs::DataStream stream(bin_pack);
    stream << (added ? Id::AddGrayList : Id::EraseGrayList) << key;
    // send zero value is senseless
    if (count_rounds > 0) {
        stream << count_rounds;
    }
    node.reportEvent(bin_pack);
}

/*static*/
bool EventReport::parseGrayListUpdate(const cs::Bytes& bin_pack, cs::PublicKey& key, uint32_t& counter) {
    if (bin_pack.empty()) {
        return false;
    }
    cs::DataStream stream(bin_pack.data(), bin_pack.size());
    Id id = Id::None;
    stream >> id;
    if (id == Id::AddGrayList || id == Id::EraseGrayList) {
        if (stream.isAvailable(key.size())) {
            stream >> key;
            // only nonzero value is sent
            if (stream.isAvailable(sizeof(uint32_t))) {
                stream >> counter;
            }
            else {
                counter = 0;
            }
            return stream.isValid() && stream.isEmpty();
        }
    }
    return false;
}

/*static*/
void EventReport::sendInvalidBlockAlarm(Node& node, const cs::PublicKey& source, cs::Sequence sequence) {
    constexpr size_t len = sizeof(EventReport::Id) + cscrypto::kPublicKeySize + sizeof(sequence);
    cs::Bytes bin_pack(len);
    cs::DataStream out(bin_pack);
    out << Id::AlarmInvalidBlock << source << sequence;
    node.reportEvent(bin_pack);
}

/*static*/
bool EventReport::parseInvalidBlockAlarm(const cs::Bytes& bin_pack, cs::PublicKey& source, cs::Sequence& sequence) {
    if (bin_pack.empty()) {
        return false;
    }
    cs::DataStream in(bin_pack.data(), bin_pack.size());
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
    constexpr size_t len = sizeof(EventReport::Id) + cscrypto::kPublicKeySize;
    cs::Bytes bin_pack(len);
    cs::DataStream out(bin_pack);
    out << problem_id << problem_source;
    node.reportEvent(bin_pack);
}

/*static*/
EventReport::Id EventReport::parseConsensusProblem(const cs::Bytes& bin_pack, cs::PublicKey& problem_source) {
    if (bin_pack.empty()) {
        return Id::None;
    }
    cs::DataStream in(bin_pack.data(), bin_pack.size());
    Id id = Id::None;
    in >> id;
    switch(id) {
    case Id::ConsensusFailed:
    case Id::ConsensusLiar:
    case Id::ConsensusSilent:
    case Id::ContractsFailed:
    case Id::ContractsLiar:
    case Id::ContractsSilent:
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
