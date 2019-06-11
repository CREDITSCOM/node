#include "csnode/fee.hpp"

#include <tuple>

#include <solver/smartcontracts.hpp>

namespace cs {
namespace {
const size_t kCommonTrSize = 152;

std::array<std::tuple<size_t, double, double>, 14> feeLevels = {
    std::make_tuple(1024 + 512, 0.008746170242, 0.0004828886573),
    std::make_tuple(20 * 1024, 0.03546746927, 0.0005862733239),
    std::make_tuple(50 * 1024, 0.1438276802, 0.001104468428),
    std::make_tuple(100 * 1024, 1.936458209, 0.009641057723),
    std::make_tuple(256 * 1024, 5.26383916, 0.3813112299),
    std::make_tuple(512 * 1024, 38.89480285, 4.874110187),
    std::make_tuple(768 * 1024, 105.7270358, 19.96925763),
    std::make_tuple(1024 * 1024, 287.3958802, 103.8255221),
    std::make_tuple(5 * 1024 * 1024, 781.2229988, 259.834061),
    std::make_tuple(15 * 1024 * 1024, 2123.584282, 651.3469549),
    std::make_tuple(50 * 1024 * 1024, 5772.500564, 2309.930666),
    std::make_tuple(100 * 1024 * 1024, 15691.28339, 5165.460461),
    std::make_tuple(500 * 1024 * 1024, 42653.3305, 9959.829026),
    std::make_tuple(1000 * 1024 * 1024, 115943.7732, 115943.7732)
};
}  // namespace

namespace fee {

csdb::AmountCommission getFee(const csdb::Transaction& t) {
    size_t size = t.to_byte_stream().size();

    if (!SmartContracts::is_smart_contract(t) && size <= kCommonTrSize) {
        return csdb::AmountCommission(kMinFee);
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

bool estimateMaxFee(const csdb::Transaction& t, csdb::AmountCommission& countedFee) {
    countedFee = getFee(t);

    if (SmartContracts::is_smart_contract(t)) {
        countedFee = csdb::AmountCommission(countedFee.to_double() +
                     std::get<2>(feeLevels[0])); // cheapest new state
    }

    return csdb::Amount(t.max_fee().to_double()) >= csdb::Amount(countedFee.to_double());
}

void setCountedFees(Transactions& trxs) {
    for (auto& t : trxs) {
        t.set_counted_fee(getFee(t));  
    }
}

csdb::Amount getExecutionFee(long long duration_msec) {
    constexpr double FEE_IN_MSEC = kMinFee * 0.001; // the cost is based on 1 kMinFee/sec
    return csdb::Amount(static_cast<double>(duration_msec) * FEE_IN_MSEC);
}

} // namespace fee
}  // namespace cs
