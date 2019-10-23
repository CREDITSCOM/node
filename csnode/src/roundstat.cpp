#include <csnode/roundstat.hpp>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <configholder.hpp>

#include <sstream>

namespace cs {
RoundStat::RoundStat()
: totalReceivedTransactions_(0)
, totalAcceptedTransactions_(0)
, deferredTransactionsCount_(0)
, totalDurationMs_(0)
, nodeStartRound_(0)
, startSkipRounds_(2)
, lastRoundMs_(0)
, roundElapseSetting_(cs::ConfigHolder::instance().config()->roundElapseTime())
, roundElapseTimePoint_(std::chrono::steady_clock::now()) {
}

void RoundStat::onRoundStart(RoundNumber round, bool skipLogs) {
    // minimal statistics, skip 0 & 1 rounds because of possibility extra timeouts
    if (startSkipRounds_ > 0) {
        startSkipRounds_--;
        nodeStartRound_ = round;
        startPointMs_ = std::chrono::steady_clock::now();
        totalDurationMs_ = 0;
    }
    else {
        using namespace std::chrono;

        auto newDurationMs = duration_cast<milliseconds>(steady_clock::now() - startPointMs_).count();
        auto lastRoundMs = cs::numeric_cast<size_t>(newDurationMs) - totalDurationMs_;
        totalDurationMs_ = cs::numeric_cast<size_t>(newDurationMs);
        size_t counter = 1;

        if (round > nodeStartRound_) {
            counter = round - nodeStartRound_;
        }

        aveRoundMs_ = totalDurationMs_ / counter;

        // TODO: use more intelligent output formatting
        if (!skipLogs) {
            std::ostringstream os;
            constexpr size_t inMinutes = 5 * 60 * 1000;
            constexpr size_t inSeconds = 10 * 1000;

            os << " last round ";

            if (lastRoundMs > inMinutes) {
                os << "> " << lastRoundMs / 60000 << "min";
            }
            else if (lastRoundMs > inSeconds) {
                os << "> " << lastRoundMs / 1000 << "sec";
            }
            else {
                os << lastRoundMs << "ms";
            }

            os << ", average round ";

            if (aveRoundMs_ > inSeconds) {
                os << "> " << aveRoundMs_ / 1000 << "sec";
            }
            else {
                os << aveRoundMs_ << "ms";
            }

            os << ", " << WithDelimiters(totalAcceptedTransactions_) << " stored transactions.";
            cslog() << os.str();
        }
    }

    resetLastRoundMs();
}

void RoundStat::onReadBlock(csdb::Pool block, bool* /*shouldStop*/) {
    totalAcceptedTransactions_ += block.transactions_count();
}

void RoundStat::onStoreBlock(csdb::Pool block) {
    totalAcceptedTransactions_ += block.transactions_count();
}

size_t RoundStat::aveTime() {
    return aveRoundMs_;
}

size_t RoundStat::nodeStartRound() {
    return nodeStartRound_;
}

size_t RoundStat::lastRoundMs() const {
    return lastRoundMs_.load(std::memory_order_acquire);
}

void RoundStat::resetLastRoundMs() {
    lastRoundMs_.store(0, std::memory_order_release);
}

bool RoundStat::isLastRoundTooLong() const {
    auto ms = lastRoundMs();
    return ms > kMaxRoundDelay;
}

void RoundStat::onPingReceived(cs::Sequence, const cs::PublicKey&) {
    static std::chrono::steady_clock::time_point point = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();
    auto result = std::chrono::duration_cast<std::chrono::milliseconds>(now - point);

    lastRoundMs_.fetch_add(static_cast<size_t>(result.count()), std::memory_order_acq_rel);
    point = now;
}

void RoundStat::onRoundChanged() {
    cs::Lock lock(roundElapseMutex_);
    roundElapseTimePoint_ = std::chrono::steady_clock::now();
}

void RoundStat::onMainThreadIterated() {
    std::chrono::steady_clock::time_point point;
    uint64_t limit = 0;

    {
        cs::Lock lock(roundElapseMutex_);
        point = roundElapseTimePoint_;
        limit = roundElapseSetting_;
    }

    auto duration = std::chrono::steady_clock::now() - point;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    if (limit > static_cast<uint64_t>(ms)) {
        return;
    }

    emit roundTimeElapsed();

    {
        // reset time point to tick next time after limit
        cs::Lock lock(roundElapseMutex_);
        roundElapseTimePoint_ = std::chrono::steady_clock::now();
    }
}

}  // namespace cs
