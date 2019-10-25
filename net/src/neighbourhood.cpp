/* Send blaming letters to @yrtimd */

#include <algorithm>
#include <iterator>
#include <random>

#include "neighbourhood.hpp"

#include <cscrypto/cscrypto.hpp>
#include <csnode/blockchain.hpp>
#include <lib/system/random.hpp>

namespace {
template <typename T>
T getSecureRandom() {
    T result;
    cscrypto::fillBufWithRandomBytes(static_cast<void*>(&result), sizeof(T));
    return result;
}

template<class InputIt>
std::vector<typename std::iterator_traits<InputIt>::value_type> sample(InputIt first, InputIt last, size_t n) {
    static std::mt19937 engine{std::random_device{}()};

    InputIt nth = next(first, static_cast<std::ptrdiff_t>(n));
    std::vector<typename std::iterator_traits<InputIt>::value_type> result{first, nth};
    size_t k = n + 1;
    for (InputIt it = nth; it != last; ++it, ++k) {
        size_t r = std::uniform_int_distribution<size_t>{0, k}(engine);
        if (r < n)
            result[r] = *it;
    }
    return result;
}

const size_t kNeighborsRedirectMin = 6;
}  // anonimous namespace
