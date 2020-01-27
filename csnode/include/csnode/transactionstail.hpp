#ifndef TRANSACTIONS_TAIL_H
#define TRANSACTIONS_TAIL_H

#include "bitheap.hpp"

#include <sstream>

#include <boost/serialization/serialization.hpp>

namespace cs {
class TransactionsTail {
public:
    static constexpr size_t BitSize = 1024;
    using TransactionId = int64_t;

public:
    bool empty() const {
        return heap_.empty();
    }

    void push(TransactionId trxId) {
        heap_.push(trxId);
    }

    TransactionId getLastTransactionId() const {
        return heap_.minMaxRange().second;
    }

    bool isAllowed(TransactionId trxId) const {
        if (heap_.empty())
            return true;
        else {
            const Heap::MinMaxRange& range = heap_.minMaxRange();
            if (trxId > range.second)
                return true;
            else if (trxId < range.first)
                return false;
            else
                return !heap_.contains(trxId);
        }
    }

    bool erase(TransactionId trxId) {
        if (heap_.empty()) {
            return false;
        }
        if (!heap_.contains(trxId)) {
            return false;
        }
        heap_.pop(trxId);
        return true;
    }

	bool isDuplicated(TransactionId trxId) const {
		if (!heap_.empty()) {
			return heap_.contains(trxId);
		}
		return false;
	}

    std::string printRange() {
        if (heap_.empty()) {
            return "any";
        }
        std::ostringstream os;
        os << '[' << heap_.minMaxRange().first << ".." << heap_.minMaxRange().second << ']';
        return os.str();
    }

private:
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive &ar, [[maybe_unused]] const unsigned int version) {
        ar & heap_;
    }

    using Heap = BitHeap<TransactionId, BitSize>;
    Heap heap_;
};

}  // namespace cs

#endif
