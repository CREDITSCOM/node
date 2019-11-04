#ifndef CSNODE_TRANSACTIONS_ITERATOR_HPP
#define CSNODE_TRANSACTIONS_ITERATOR_HPP

#include <csdb/address.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>

class BlockChain;

namespace cs {

class TransactionsIterator {
public:
    TransactionsIterator(const BlockChain&, const csdb::Address&);
    TransactionsIterator(const BlockChain&, const csdb::Address&, const csdb::Pool&);

    void next();
    bool isValid() const;

    const csdb::Pool& getPool() const {
        return lapoo_;
    }

    const csdb::Transaction& operator*() const {
        return *it_;
    }
    auto operator-> () const {
        return it_;
    }

private:
    void setFromTransId(const csdb::TransactionID&);

    const BlockChain& bc_;

    csdb::Address addr_;
    csdb::Pool lapoo_;
    std::vector<csdb::Transaction>::const_reverse_iterator it_;
};

} // namespace cs
#endif // CSNODE_TRANSACTIONS_ITERATOR_HPP
