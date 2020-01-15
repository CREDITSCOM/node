#include "csnode/fee.hpp"

#include <tuple>

#include <solver/smartcontracts.hpp>

namespace cs {
namespace {
const size_t kCommonTrSize = 152;

// all the values are conform to AmountComission capabilities (source -> AmountComission -> result => source == result)
constexpr std::array<std::tuple<size_t, double, double>, 14> feeLevels = {
    std::make_tuple(5 * 1024,    0.087402,  0.008740), // this is min fee value
    std::make_tuple(20 * 1024,   0.174805,  0.034961),
    std::make_tuple(50 * 1024,   0.437500,  0.139648),
    std::make_tuple(100 * 1024,  17.87109,  17.87109),
    std::make_tuple(256 * 1024,  89.55078,  89.55078),
    std::make_tuple(512 * 1024,  358.39844, 358.39844),
    std::make_tuple(768 * 1024,  1435.5469, 1435.5469),
    std::make_tuple(1024 * 1024, 5732.4219, 5732.4219),
    std::make_tuple(5 * 1024 * 1024,    22949.21875, 22949.21875),
    std::make_tuple(15 * 1024 * 1024,   91699.21875, 91699.21875),
    std::make_tuple(50 * 1024 * 1024,   367187.5,    367187.5),
    std::make_tuple(100 * 1024 * 1024,  1464843.75,  1464843.75),
    std::make_tuple(500 * 1024 * 1024,  5869140.625, 5869140.625),
    std::make_tuple(1000 * 1024 * 1024, 23437500.0,  23437500.0)
};

constexpr double minFee() {
    return std::get<2>(feeLevels[0]);
}

constexpr double minContractStateFee() {
    return minFee();
}

}  // namespace

namespace fee {

csdb::AmountCommission getFee(const csdb::Transaction& t) {
    size_t size = t.to_byte_stream().size();

    if (!SmartContracts::is_smart_contract(t) && size <= kCommonTrSize) {
        return csdb::AmountCommission(minFee());
    }

    for (const auto& level : feeLevels) {
        if (size < std::get<0>(level)) {
            if (SmartContracts::is_deploy(t)) {
                return csdb::AmountCommission(std::get<1>(level));
            }
            return csdb::AmountCommission(std::get<2>(level));
        }
    }

    double k = static_cast<double>(size) / std::get<0>(feeLevels[feeLevels.size() - 1]);
    return csdb::AmountCommission(std::get<1>(feeLevels[feeLevels.size() - 1]) * k);
}

csdb::AmountCommission getContractStateMinFee() {
    return csdb::AmountCommission(minContractStateFee()); // cheapest new state
}

bool estimateMaxFee(const csdb::Transaction& t, csdb::AmountCommission& countedFee, SmartContracts& sc) {
    countedFee = getFee(t);

    if (SmartContracts::is_executable(t) || sc.is_payable_call(t)) {
        countedFee = csdb::AmountCommission(countedFee.to_double() + getContractStateMinFee().to_double());
    }

    return (t.max_fee().to_double() - countedFee.to_double() >= 0);
}

void setCountedFees(Transactions& trxs) {
    for (auto& t : trxs) {
        if (!cs::SmartContracts::is_new_state(t)) {
            t.set_counted_fee(getFee(t));
        }
    }
}

csdb::Amount getExecutionFee(long long duration_mcs) {
    constexpr double FEE_IN_MCS = minContractStateFee() * 0.001 * 0.001; // the cost is based on 1 kMinFee/sec
    return csdb::Amount(static_cast<double>(duration_mcs) * FEE_IN_MCS);
}

} // namespace fee
}  // namespace cs
