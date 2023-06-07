#include "nodecore.hpp"
#include <datastream.hpp>

namespace cs {
Zero::Zero() {
    hash.fill(0);
    signature.fill(0);
    key.fill(0);
    timeStamp = 0ULL;
}
TimeMoney::TimeMoney(const uint64_t it, const uint64_t t, const csdb::Amount am){
    initialTime = it/1000;
    time = t;
    amount = am; 
    uint64_t ThreeMonthMilliseconds = 7862400ULL;
    uint64_t m = 0ULL;
    if (time == cs::Zero::timeStamp || time < initialTime) {
        csdebug() << "TimeMoney: setting coeff to zero: it = " << initialTime << ", t = " << time;
        coeff = StakingCoefficient::NoStaking;
    }
    else {
        m = (time - initialTime) / ThreeMonthMilliseconds;
        csdebug() << "TimeMoney: calculating coeff, m: " << m;
        switch (m) {
        case 0:
            coeff = StakingCoefficient::NoStaking;
            break;
        case 1: 
            coeff = StakingCoefficient::ThreeMonth;
            break;
        case 2:
            coeff = StakingCoefficient::SixMonth;
            break;
        case 3:
            coeff = StakingCoefficient::NineMonth;
            break;
        case 4:
            coeff = StakingCoefficient::Anni;
            break;
        default:
            coeff = StakingCoefficient::Anni;
        }
    }
    csdebug() << "TimeMoney created: initial time " << initialTime << ", final time: " << time << ", amount: " << amount.to_string() << ", coeff = " << static_cast<int>(coeff) << ", m = " << m;

    
}
}  // namespace cs

namespace {
// the only purpose of the object is to init static members
static const cs::Zero zero;
}  // namespace

std::size_t std::hash<cs::TransactionsPacketHash>::operator()(const cs::TransactionsPacketHash& packetHash) const noexcept {
    const std::size_t p = 16777619;
    std::size_t hash = 2166136261;

    auto data = packetHash.toBinary();
    auto size = data.size();

    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * p;
    }

    hash += hash << 13;
    hash ^= hash >> 7;
    hash += hash << 3;
    hash ^= hash >> 17;
    hash += hash << 5;

    return hash;
}

cs::Bytes cs::RoundTable::toBinary() const {
    cs::Bytes bytes;
    cs::ODataStream stream(bytes);

    stream << round;
    stream << confidants;

    return bytes;
}
