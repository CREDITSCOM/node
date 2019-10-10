#ifndef RANDOM_HPP
#define RANDOM_HPP

#include <random>
#include <algorithm>

namespace cs {
class Random {
    // random value generation helper
    template <typename R, typename T>
    static R randomValueImpl(T min, T max) {
        static std::random_device randomDevice;
        static std::mt19937 generator(randomDevice());

        if constexpr (std::is_integral_v<T>) {
            std::uniform_int_distribution<> dist(min, max);
            return static_cast<R>(dist(generator));
        }
        else {
            std::uniform_real_distribution<> distribution(min, max);
            return static_cast<R>(distribution(generator));
        }
    }

public:
    ///
    /// Generates random value from random generator [min, max] for integer type
    ///
    template <typename R>
    inline static R generateValue(int min, int max) {
        return randomValueImpl<R, decltype(min)>(min, max);
    }

    ///
    /// Generates random value from random generator [min, max] for floating point type
    ///
    template <typename R>
    inline static R generateValue(double min, double max) {
        return randomValueImpl<R, decltype(min)>(min, max);
    }

    ///
    /// Random shuffles range of [first, last).
    /// Same sa msvc stl realization from https://github.com/microsoft/STL/blob/master/stl/inc/algorithm
    ///
    template <typename RandomIterator, typename Generator>
    static void shuffle(RandomIterator first, RandomIterator last, Generator&& rng) {
        // shuffle [first, last) using random function rng
        auto uFirst = first;
        const auto uLast = last;

        if (uFirst == uLast) {
            return;
        }

        using diff = typename std::iterator_traits<RandomIterator>::difference_type;
        using udiff = typename std::make_unsigned<diff>::type;

        udiff bits = 8 * sizeof(udiff);
        udiff bmask = udiff(-1);

        auto target = uFirst;
        diff targetIndex = 1;

        for (; ++target != uLast; ++targetIndex) {
            // randomly place an element from [first, target] at target
            diff off;  // = rng(static_cast<_Diff>(targetIndex + 1));
            diff index = targetIndex + 1;

            for (;;) {
                // try a sample random value
                udiff ret = 0;   // random bits
                udiff mask = 0;  // 2^N - 1, _Ret is within [0, mask]

                while (mask < udiff(index - 1)) {
                    // need more random bits
                    ret <<= bits - 1;  // avoid full shift
                    ret <<= 1;

                    udiff _Get_bits;
                    // return a random value within [0, bmask]
                    for (;;) {
                        // repeat until random value is in range
                        udiff _Val = rng();

                        if (_Val <= bmask) {
                            _Get_bits = _Val;
                            break;
                        }
                    }

                    ret |= _Get_bits;
                    mask <<= bits - 1;  // avoid full shift
                    mask <<= 1;
                    mask |= bmask;
                }

                // _Ret is [0, mask], index - 1 <= mask, return if unbiased
                if (ret / index < mask / index || mask % index == udiff(index - 1)) {
                    off = static_cast<diff>(ret % index);
                    break;
                }
            }

            std::iter_swap(target, uFirst + off);
        }
    }
};
}

#endif  //  RANDOM_HPP
