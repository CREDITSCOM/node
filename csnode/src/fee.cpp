#include "csnode/fee.hpp"

#include <tuple>

#include <csdb/amount_commission.hpp>
#include <csdb/transaction.hpp>
#include <solver/smartcontracts.hpp>

namespace cs {
namespace {
const size_t kCommonTrSize = 152;

std::array<std::tuple<size_t, double, double>, 16> feeLevels = {
    std::make_tuple(kCommonTrSize, kMinFee, kMinFee),
    std::make_tuple(kCommonTrSize * 2, kMinFee * 5, kMinFee * 3),
    std::make_tuple(1024 + 512, 0.008746170242, 0.004828886573),
    std::make_tuple(20 * 1024, 0.03546746927, 0.005862733239),
    std::make_tuple(50 * 1042, 0.1438276802, 0.01104468428),
    std::make_tuple(100 * 1024, 1.936458209, 0.09641057723),
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

const size_t kSizePos = 0;
const size_t kDepPos = 1;
const size_t kCommonPos = 2;
}  // namespace

namespace fee {

csdb::AmountCommission getFee(const csdb::Transaction& t) {
    size_t size = t.to_byte_stream().size();
    for (auto it = feeLevels.cbegin(); it != feeLevels.cend(); ++it) {
        if (size < std::get<kSizePos>(*it)) {
            if (SmartContracts::is_deploy(t)) {
                return csdb::AmountCommission(std::get<kDepPos>(*it));
            } else {
                return csdb::AmountCommission(std::get<kCommonPos>(*it));
            }
        }
    }

    double k = static_cast<double>(size) / std::get<kSizePos>(feeLevels[feeLevels.size() - 1]);
    if (SmartContracts::is_deploy(t)) {
        return csdb::AmountCommission(std::get<kDepPos>(feeLevels[feeLevels.size() -1]) * k);
    } else {
        return csdb::AmountCommission(std::get<kCommonPos>(feeLevels[feeLevels.size() -1]) * k);
    }
}

bool estimateMaxFee(const csdb::Transaction& t, csdb::AmountCommission& countedFee) {
    countedFee = getFee(t);
    return csdb::Amount(t.max_fee().to_double()) >= csdb::Amount(countedFee.to_double());
}

void setCountedFees(Transactions& trxs) {
    for (auto& t : trxs) {
        t.set_counted_fee(getFee(t));  
    }
}
} // namespace fee
}  // namespace cs
