#include <csnode/roundstat.hpp>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <lib/system/concurrent.hpp>

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
, roundElapseTimePoint_(std::chrono::steady_clock::now())
, storeBlockElapseTimePoint_(std::chrono::steady_clock::now()) {
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

            os << ", " << WithDelimiters(uint64_t(totalAcceptedTransactions_)) << " stored transactions.";
            cslog() << os.str();
        }
    }

    resetLastRoundMs();
}

void RoundStat::onReadBlock(const csdb::Pool& block, bool* /*shouldStop*/) {
    totalAcceptedTransactions_ += block.transactions_count();
}

void RoundStat::onStoreBlock(const csdb::Pool& block) {
    totalAcceptedTransactions_ += block.transactions_count();
}

size_t RoundStat::uptimeMs() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now() - startPointMs_).count();
}

size_t RoundStat::aveRoundMs() const {
    return aveRoundMs_;
}

size_t RoundStat::nodeStartRound() const {
    return nodeStartRound_;
}

size_t RoundStat::lastRoundMs() const {
    return lastRoundMs_.load(std::memory_order_acquire);
}

void RoundStat::resetLastRoundMs() {
    lastRoundMs_.store(0, std::memory_order_release);
}

bool RoundStat::isCurrentRoundTooLong(size_t longDurationMs) const {
    auto ms = lastRoundMs();
    return ms >= longDurationMs;
}

void RoundStat::onPingReceived(cs::Sequence, const cs::PublicKey&) {
    static std::chrono::steady_clock::time_point point = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();
    auto result = std::chrono::duration_cast<std::chrono::milliseconds>(now - point);

    lastRoundMs_.fetch_add(static_cast<size_t>(result.count()), std::memory_order_acq_rel);
    point = now;
}

void RoundStat::checkPing(cs::Sequence sequence, const PublicKey& key) {
    static std::chrono::steady_clock::time_point point = std::chrono::steady_clock::now();
    static std::pair<cs::PublicKey, cs::Sequence> maxSequenceNeighbour{};

    if (maxSequenceNeighbour.second < sequence) {
        maxSequenceNeighbour = std::make_pair(key, sequence);
    }

    auto now = std::chrono::steady_clock::now();
    checkPingDelta_ += std::chrono::duration_cast<std::chrono::milliseconds>(now - point);

    if (RoundStat::kMaxPingSynchroDelay <= checkPingDelta_.count()) {
        checkPingDelta_ = std::chrono::milliseconds(0);
        emit pingChecked(maxSequenceNeighbour.second, maxSequenceNeighbour.first);
    }

    point = now;
}

void RoundStat::onRoundChanged() {
    cs::Lock lock(statsElapseMutex_);
    roundElapseTimePoint_ = std::chrono::steady_clock::now();
}

void RoundStat::onBlockStored() {
    {
        cs::Lock lock(statsElapseMutex_);
        storeBlockElapseTimePoint_ = std::chrono::steady_clock::now();
    }

    checkPingDelta_ = std::chrono::milliseconds(0);
}

void RoundStat::onMainThreadIterated() {
    checkRoundElapse();
    checkStoreBlockElapse();
}

void RoundStat::checkRoundElapse() {
    std::chrono::steady_clock::time_point point;
    uint64_t limit = 0;

    {
        cs::Lock lock(statsElapseMutex_);
        point = roundElapseTimePoint_;
        limit = cs::ConfigHolder::instance().config()->roundElapseTime();
    }

    auto duration = std::chrono::steady_clock::now() - point;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    if (limit > static_cast<uint64_t>(ms)) {
        return;
    }

    cs::Concurrent::execute(cs::RunPolicy::CallQueuePolicy, [this] {
        emit roundTimeElapsed();
    });

    {
        // reset time point to tick next time after limit
        cs::Lock lock(statsElapseMutex_);
        roundElapseTimePoint_ = std::chrono::steady_clock::now();
    }
}

void RoundStat::checkStoreBlockElapse() {
    std::chrono::steady_clock::time_point point;
    uint64_t limit = 0;

    {
        cs::Lock lock(statsElapseMutex_);
        point = storeBlockElapseTimePoint_;
        limit = cs::ConfigHolder::instance().config()->storeBlockElapseTime();
    }

    auto duration = std::chrono::steady_clock::now() - point;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    if (limit > static_cast<uint64_t>(ms)) {
        return;
    }

    cs::Concurrent::execute(cs::RunPolicy::CallQueuePolicy, [this] {
        emit storeBlockTimeElapsed();
    });

    {
        cs::Lock lock(statsElapseMutex_);
        storeBlockElapseTimePoint_ = std::chrono::steady_clock::now();
    }
}
}  // namespace cs
