#include <csnode/transactionsiterator.hpp>

#include <csdb/address.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <csnode/blockchain.hpp>

namespace cs {

TransactionsIterator::TransactionsIterator(BlockChain& bc,
                                           const csdb::Address& addr)
        : bc_(bc) , addr_(addr) {
    setFromTransId(bc_.getLastTransaction(addr));
}

void TransactionsIterator::setFromTransId(const csdb::TransactionID& lTrans) {
    if (lTrans.is_valid()) {
        lapoo_ = bc_.loadBlock(lTrans.pool_seq());
        it_ = lapoo_.transactions().rbegin()
            + (lapoo_.transactions().size() - lTrans.index() - 1);
    }
    else {
        lapoo_ = csdb::Pool{};
    }
}

bool TransactionsIterator::isValid() const {
    return lapoo_.is_valid();
}

void TransactionsIterator::next() {
    while (++it_ != lapoo_.transactions().rend()) {
        if (bc_.isEqual(it_->source(), addr_)
            || bc_.isEqual(it_->target(), addr_)) {
            break;
        }
    }

    if (it_ == lapoo_.transactions().rend()) {
        auto ps = bc_.getPreviousPoolSeq(addr_, lapoo_.sequence());
        lapoo_ = bc_.loadBlock(ps);

        if (lapoo_.is_valid()) {
            it_ = lapoo_.transactions().rbegin();
            // transactions() cannot be empty
            if (!bc_.isEqual(it_->source(), addr_) && !bc_.isEqual(it_->target(), addr_)) {
                next();  // next should be executed only once
            }
        }
    }
}
} // namespace cs
