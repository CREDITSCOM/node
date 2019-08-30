#include <csnode/transactionsiterator.hpp>

#include <csnode/blockchain.hpp>
#include <lib/system/logger.hpp>

namespace cs {

TransactionsIterator::TransactionsIterator(BlockChain& bc,
                                           const csdb::Address& addr)
        : bc_(bc),
          addr_(bc_.getAddressByType(addr, BlockChain::AddressType::PublicKey)) {
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
    // no more transactions in lapoo_ with addr_
    // load previous pool from blockchain
        auto ps = bc_.getPreviousPoolSeq(addr_, lapoo_.sequence());
        lapoo_ = bc_.loadBlock(ps);

        while (lapoo_.is_valid() && !lapoo_.transactions_count()) {
        // case of inconsistent index.db
            cserror() << "TransactionsIterator: "
                      << "Empty pool in transactions index detected: "
                      << "sequence is " << lapoo_.sequence()
                      << " , address is " << addr_.to_string();
            ps = bc_.getPreviousPoolSeq(addr_, lapoo_.sequence());
            lapoo_ = bc_.loadBlock(ps);
        }

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
