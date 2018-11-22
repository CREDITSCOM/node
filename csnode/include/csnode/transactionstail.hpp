#ifndef TRANSACTIONS_TAIL_H
#define TRANSACTIONS_TAIL_H

#include "bitheap.hpp"

namespace cs {
class TransactionsTail {
public:
  static constexpr size_t BitSize = 32;
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

private:
  using Heap = BitHeap<TransactionId, BitSize>;
  Heap heap_;
};

}  // namespace cs

#endif
