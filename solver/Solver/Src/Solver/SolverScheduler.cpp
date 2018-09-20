#include "Solver/Solver.hpp"
#include <csnode/node.hpp>
#include "Solver/Generals.hpp"

namespace Credits {

    //TODO: duplicated from Solver.cpp, move to header file?
    constexpr short min_nodes = 3;

    void Solver::doSelfTest()
    {
        auto lvl = node_->getMyLevel();
        auto rnum = node_->getRoundNumber();

        bool is_writer = (lvl == NodeLevel::Writer);
        bool is_trusted = (lvl == NodeLevel::Confidant);

        bool test_block = true;
        bool test_hashes = is_writer;
        bool test_tl = is_trusted;
        bool test_vect = is_trusted;
        bool test_matr = is_trusted;
        bool test_consensus = is_trusted;

        if(timer_used) {
            timer_service.TimeConsoleOut("doSelfTest() output begin", rnum);
            std::cout << "|   Round " << rnum << std::endl;

            if(test_block && !gotBlockThisRound) {
                std::cout << "|   gotBlockThisRound: no" << std::endl;
            }
            if(test_tl && !transactionListReceived) {
                std::cout << "|   transactionListReceived: no" << std::endl;
            }
            if(test_vect && !vectorComplete) {
                std::cout << "|   vectorComplete: no" << std::endl;
            }
            if(test_matr && !allMatricesReceived) {
                std::cout << "|   allMatricesReceived: no" << std::endl;
            }
            if(test_consensus && !consensusAchieved) {
                std::cout << "|   consensusAchieved: no" << std::endl;
            }
            if(test_hashes) {
                size_t cnt = ips.size();
                std::cout << "|   hashes received: " << cnt;
                if(cnt < min_nodes) {
                    std::cout << " (desired: " << min_nodes << ")";
                }
                std::cout << std::endl;
            }
            std::cout << "+-- doSelfTest output end" << std::endl;
        }
    }

    void Solver::scheduleReqRoundTable(uint32_t wait_for_ms, size_t round_num)
    {
        if(timer_used)
        {
            std::ostringstream os;
            os << "schedule (" << wait_for_ms << " ms) sendRoundTableRequest(" << round_num << ")";
            timer_service.TimeConsoleOut(os.str(), round_num);
        }
        tagReqRoundTable = calls_scheduler.InsertOnce(wait_for_ms, [this, round_num]() {
            if(timer_used) {
                // log with actual current round
                timer_service.TimeConsoleOut("sendRoundTableRequest()", node_->getRoundNumber());
            }
            // call with scheduled round number!
            node_->sendRoundTableRequest(round_num);
            tagReqRoundTable = no_tag;
        });
    }

    void Solver::scheduleReqTransactionList(uint32_t wait_for_ms)
    {
        if(timer_used)
        {
            std::ostringstream os;
            os << "schedule (" << wait_for_ms << " ms) sendTLRequest()";
            timer_service.TimeConsoleOut(os.str(), node_->getRoundNumber());
        }
        tagReqTransactionList = calls_scheduler.InsertOnce(wait_for_ms, [this]() {
            if(timer_used) {
                timer_service.TimeConsoleOut("sendTLRequest()", node_->getRoundNumber());
            }
            node_->sendTLRequest();
            tagReqTransactionList = no_tag;
        });
    }

    void Solver::scheduleReqVectors(uint32_t wait_for_ms)
    {
        if(timer_used)
        {
            std::ostringstream os;
            os << "schedule (" << wait_for_ms << " ms) requestMissingVectors()";
            timer_service.TimeConsoleOut(os.str(), node_->getRoundNumber());
        }
        tagReqVectors = calls_scheduler.InsertOnce(wait_for_ms, [this]() {
            if(timer_used) {
                timer_service.TimeConsoleOut("requestMissingVectors()", node_->getRoundNumber());
            }
            requestMissingVectors();
            tagReqVectors = no_tag;
        });
    }

    void Solver::scheduleReqMatrices(uint32_t wait_for_ms)
    {
        if(timer_used)
        {
            std::ostringstream os;
            os << "schedule (" << wait_for_ms << " ms) requestMissingMatrices()";
            timer_service.TimeConsoleOut(os.str(), node_->getRoundNumber());
        }
        tagReqMatrices = calls_scheduler.InsertOnce(wait_for_ms, [this]() {
            if(timer_used) {
                timer_service.TimeConsoleOut("requestMissingMatrices()", node_->getRoundNumber());
            }
            requestMissingMatrices();
            tagReqMatrices = no_tag;
        });
    }

    void Solver::scheduleReqBlock(uint32_t wait_for_ms)
    {
        if(timer_used)
        {
            std::ostringstream os;
            os << "schedule (" << wait_for_ms << " ms) sendBlockRequest()";
            timer_service.TimeConsoleOut(os.str(), node_->getRoundNumber());
        }
        tagReqBlock = calls_scheduler.InsertOnce(wait_for_ms, [this]() {
            auto rnum = node_->getRoundNumber();
            if(timer_used) {
                timer_service.TimeConsoleOut("sendBlockRequest()", rnum);
            }
            node_->sendBlockRequest(rnum);
            tagReqBlock = no_tag;
        });
    }

    void Solver::scheduleReqHashes(uint32_t wait_for_ms)
    {
        if(timer_used)
        {
            std::ostringstream os;
            os << "schedule (" << wait_for_ms << " ms) requestMissingHashes()";
            timer_service.TimeConsoleOut(os.str(), node_->getRoundNumber());
        }
        tagReqHashes = calls_scheduler.InsertOnce(wait_for_ms, [this]() {
            if(timer_used) {
                timer_service.TimeConsoleOut("requestMissingHashes()", node_->getRoundNumber());
            }
            requestMissingHashes();
            tagReqHashes = no_tag;
        });
    }

    void Solver::scheduleWriteNewBlock(uint32_t wait_for_ms)
    {
        if(timer_used) {
            std::ostringstream os;
            os << "schedule (" << wait_for_ms << " ms) writeNewBlock()";
            timer_service.TimeConsoleOut(os.str(), node_->getRoundNumber());
        }
        tagWriteNewBlock = calls_scheduler.InsertOnce(wait_for_ms, [this]() {
            if(timer_used) {
                timer_service.TimeConsoleOut("writeNewBlock()", node_->getRoundNumber());
            }
            writeNewBlock();
            tagWriteNewBlock = no_tag;
        });
    }

    void Solver::scheduleCloseMainRound(uint32_t wait_for_ms)
    {
        if(timer_used) {
            std::ostringstream os;
            os << "schedule (" << wait_for_ms << " ms) closeMainRound()";
            timer_service.TimeStore(os.str(), node_->getRoundNumber());
        }
        tagCloseMainRound = calls_scheduler.InsertOnce(wait_for_ms, [this]() {
            if(timer_used) {
                timer_service.TimeConsoleOut("closeMainRound()", node_->getRoundNumber());
            }
            closeMainRound();
            tagCloseMainRound = no_tag;
        });
    }

    void Solver::scheduleOnRoundExpired(uint32_t wait_for_ms)
    {
        if(timer_used) {
            std::ostringstream os;
            os << "Shedule (" << wait_for_ms << ") onRoundExpired()";
            timer_service.TimeConsoleOut(os.str(), node_->getRoundNumber());
        }
        tagOnRoundExpired = calls_scheduler.InsertOnce(wait_for_ms, [this]() {
            if(timer_used) {
                timer_service.TimeConsoleOut("onRoundExpired()", node_->getRoundNumber());
            }
            doSelfTest();
            tagOnRoundExpired = no_tag;
        });
    }

    void Solver::scheduleFlushTransactions(uint32_t period_ms)
    {
        if(timer_used) {
            std::ostringstream os;
            os << "Shedule (period " << period_ms << " ms) flushTransactions()";
            timer_service.TimeConsoleOut(os.str(), node_->getRoundNumber());
        }
        tagFlushTransactions = calls_scheduler.InsertPeriodic(period_ms, [this]() {
            //if(timer_used) {
            //    timer_service.TimeConsoleOut("flushTransactions()", currentRound);
            //}
            flushTransactions();
            // do not set tagFlushTransactions to no_tag!!!
        });
    }

    void Solver::cancelReqRoundTable()
    {
        if(tagReqRoundTable != no_tag) {
            if(timer_used) {
                timer_service.TimeConsoleOut("cancel sendRoundTableRequest()", node_->getRoundNumber());
            }
            calls_scheduler.Remove(tagReqRoundTable);
            tagReqRoundTable = no_tag;
        }
    }

    void Solver::cancelReqTransactionList()
    {
        if(tagReqTransactionList != no_tag) {
            if(timer_used) {
                timer_service.TimeConsoleOut("cancel sendTLRequest()", node_->getRoundNumber());
            }
            calls_scheduler.Remove(tagReqTransactionList);
            tagReqTransactionList = no_tag;
        }
    }

    void Solver::cancelReqVectors()
    {
        if(tagReqVectors != no_tag) {
            if(timer_used) {
                timer_service.TimeConsoleOut("cancel requestMissingVectors()", node_->getRoundNumber());
            }
            calls_scheduler.Remove(tagReqVectors);
            tagReqVectors = no_tag;
        }
    }

    void Solver::cancelReqMatrices()
    {
        if(tagReqMatrices != no_tag) {
            if(timer_used) {
                timer_service.TimeConsoleOut("cancel requestMissingMatrices()", node_->getRoundNumber());
            }
            calls_scheduler.Remove(tagReqMatrices);
            tagReqMatrices = no_tag;
        }
    }

    void Solver::cancelReqBlock()
    {
        if(tagReqBlock != no_tag) {
            if(timer_used) {
                timer_service.TimeConsoleOut("cancel sendBlockRequest()", node_->getRoundNumber());
            }
            calls_scheduler.Remove(tagReqBlock);
            tagReqBlock = no_tag;
        }
    }

    void Solver::cancelReqHashes()
    {
        if(tagReqHashes != no_tag) {
            if(timer_used) {
                timer_service.TimeConsoleOut("cancel requestMissingHashes()", node_->getRoundNumber());
            }
            calls_scheduler.Remove(tagReqHashes);
            tagReqHashes = no_tag;
        }
    }

    void Solver::cancelWriteNewBlock()
    {
        if(tagWriteNewBlock != no_tag) {
            if(timer_used) {
                timer_service.TimeConsoleOut("cancel writeNewBlock()()", node_->getRoundNumber());
            }
            calls_scheduler.Remove(tagWriteNewBlock);
            tagWriteNewBlock = no_tag;
        }
    }

    void Solver::cancelCloseMainRound()
    {
        if(tagCloseMainRound != no_tag) {
            if(timer_used) {
                timer_service.TimeConsoleOut("cancel closeMainRound()", node_->getRoundNumber());
            }
            calls_scheduler.Remove(tagCloseMainRound);
            tagCloseMainRound = no_tag;
        }
    }

    void Solver::cancelOnRoundExpired()
    {
        if(tagOnRoundExpired != no_tag) {
            if(timer_used) {
                timer_service.TimeConsoleOut("cancel onRoundExpired()", node_->getRoundNumber());
            }
            calls_scheduler.Remove(tagOnRoundExpired);
            tagOnRoundExpired = no_tag;
        }
    }

    void Solver::cancelFlushTransactions()
    {
        if(tagFlushTransactions != no_tag) {
            if(timer_used) {
                timer_service.TimeConsoleOut("cancel flushTransactions()", node_->getRoundNumber());
            }
            calls_scheduler.Remove(tagFlushTransactions);
            tagFlushTransactions = no_tag;
        }
    }

    // makes and send request to T-nodes those matrices are still absent this round
    void Solver::requestMissingMatrices()
    {
        //auto matrix = generals->getMatrix();
        auto conf = node_->getConfidants();
        size_t cnt = conf.size();
        for(size_t i = 0; i < cnt; ++i) {
            //auto sender = matrix.hmatr [i].Sender;
            if(!receivedMatFrom [i]) {
                node_->sendMatrixRequest(*(conf.cbegin() + i));
            }
        }
    }

    // makes and send request to T-nodes those matrices are still absent this round
    void Solver::requestMissingVectors()
    {
        //auto matrix = generals->getMatrix();
        auto conf = node_->getConfidants();
        size_t cnt = conf.size();
        for(size_t i = 0; i < cnt; ++i) {
            //auto sender = matrix.hmatr [i].Sender;
            if(!receivedVecFrom [i]) {
                node_->sendVectorRequest(*(conf.cbegin() + i));
            }
        }
    }

    // makes and send request to T-nodes those hashes are still absent this round
    void Solver::requestMissingHashes()
    {
        //TODO: request hashes from absent?
    }

} // namespace Credits
