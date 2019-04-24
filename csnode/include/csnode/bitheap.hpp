#ifndef BITHEAP_H
#define BITHEAP_H

#include <bitset>
#include <climits>
#include <limits>

namespace cs {
template <typename T, size_t BitSize = sizeof(T) * CHAR_BIT, typename = std::enable_if<std::is_integral<T>::value>>
class BitHeap {
public:
    using MinMaxRange = std::pair<T, T>;

public:
    BitHeap()
    : greatest_(std::numeric_limits<T>::max())
    , isValueSet_(false) {
    }

    void push(T val) {
        if (!isValueSet_) {
            greatest_ = val;
            isValueSet_ = true;
            return;
        }

        if (val > greatest_) {
            T shift = val - greatest_;
            bits_ <<= shift;
            // curr greatest
            size_t ind = shift - 1;
            if (ind < BitSize)
                bits_.set(ind);
            // new greatest
            greatest_ = val;
        }
        else if (val < greatest_) {
            size_t ind = greatest_ - val - 1;
            if (ind < BitSize)
                bits_.set(ind);
        }
    }

    void pop(T val) {
        if (val < greatest_) {
            size_t ind = greatest_ - val - 1;
            if (ind < BitSize) {
                bits_.reset(ind);
            }
            return;
        }
        if (val == greatest_) {
            --greatest_;
            int count = BitSize;
            while (!bits_[0] && count--) {
                bits_ >>= 1;
                --greatest_;
            }
            if (count < 0) {
                isValueSet_ = false;
                return;
            }
            bits_ >>= 1;
        }
    }

    bool empty() const {
        return !isValueSet_;
    }

    MinMaxRange minMaxRange() const {
        return std::make_pair(greatest_ - BitSize, greatest_);
    }

    bool contains(T val) const {
        if (val > greatest_)
            return false;
        else if (val == greatest_)
            return true;
        else {
            size_t ind = greatest_ - val - 1;
            if (ind < BitSize)
                return bits_.test(ind) == 1;
            else
                return false;
        }
    }

    size_t count() const {
        if (empty())
            return 0;
        else
            return 1 + bits_.count();
    }

private:
    T greatest_;
    uint8_t isValueSet_;
    std::bitset<BitSize> bits_;
};

}  // namespace cs

#endif
