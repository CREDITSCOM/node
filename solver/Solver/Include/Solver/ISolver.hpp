//
// Created by alexraag on 01.05.2018.
//

#pragma once

#include <functional>
#include <string>
#include <set>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

#include <boost/asio.hpp>
#include <api_types.h>
#include <csdb/transaction.h>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>

namespace Credits{
    typedef std::string Vector;
    typedef std::string Matrix;

    class ISolver{
    public:
        explicit ISolver() { }
		virtual ~ISolver() { };

		virtual void gotTransaction(csdb::Transaction&&) = 0;
		virtual void gotTransactionList(csdb::Transaction&&) = 0;
		virtual void gotBlockCandidate(csdb::Pool&&) = 0;
		virtual void gotVector(Vector&&, const PublicKey&) = 0;
		virtual void gotMatrix(Matrix&&, const PublicKey&) = 0;
		virtual void gotBlock(csdb::Pool&&, const PublicKey&) = 0;
		virtual void gotHash(Hash&&, const PublicKey&) = 0;

		virtual void nextRound() = 0;

		virtual void addInitialBalance() = 0;

        virtual void send_wallet_transaction(const csdb::Transaction& transaction) = 0;
    };
}
