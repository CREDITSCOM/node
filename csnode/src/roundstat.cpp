#include <csnode/roundstat.hpp>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <lib/system/concurrent.hpp>
#include "datastream.hpp"
#include <configholder.hpp>

#include <sstream>

namespace cs {
RoundStat::RoundStat(BlockChain* bch)
: totalReceivedTransactions_(0)
, totalAcceptedTransactions_(0)
, deferredTransactionsCount_(0)
, totalDurationMs_(0)
, nodeStartRound_(0)
, startSkipRounds_(2)
, lastRoundMs_(0)
, roundElapseTimePoint_(std::chrono::steady_clock::now())
, storeBlockElapseTimePoint_(std::chrono::steady_clock::now()) {
    //nodes_ = new std::map<cs::PublicKey, cs::NodeStat>();
    blockChain_ = bch;
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

void RoundStat::dayChangeProcedure(uint64_t cTime) {
    csdebug() << __func__;
    auto it = nodes_.begin();
    while (it != nodes_.end()) {
        it->second.failedTrustedDay = 0;
        it->second.feeDay = csdb::Amount{ 0 };
        it->second.rewardDay = csdb::Amount{ 0 };
        it->second.trustedDay = 0ULL;
        it->second.failedTrustedADay = 0ULL;
        it->second.trustedADay = 0ULL;
        if (cTime > it->second.lastConsensus + daySeconds) {
            it->second.nodeOn = false;
            it->second.timeActive = 0ULL;
        }
        ++it;
    }

    auto rewIt = minedEvaluation_.begin();
    while (rewIt != minedEvaluation_.end()) {
        auto delIt = rewIt->second.me.begin();
        while (delIt != rewIt->second.me.end()) {
            delIt->second.rewardDay = csdb::Amount{ 0 };
            ++delIt;
        }
        ++rewIt;
    }

    totalMined_.rewardDay = csdb::Amount{ 0 };
}

void RoundStat::monthChangeProcedure() {
    //csdebug() << __func__;
    auto it = nodes_.begin();
    while (it != nodes_.end()) {
        it->second.failedTrustedPrevMonth = it->second.failedTrustedMonth;
        it->second.failedTrustedMonth = 0ULL;
        it->second.feePrevMonth = it->second.feeMonth;
        it->second.feeMonth = csdb::Amount{ 0 };
        it->second.trustedPrevMonth = it->second.trustedMonth;
        it->second.trustedMonth = 0ULL;
        it->second.failedTrustedAPrevMonth = it->second.failedTrustedAMonth;
        it->second.failedTrustedAMonth = 0ULL;
        it->second.trustedAPrevMonth = it->second.trustedAMonth;
        it->second.trustedAMonth = 0ULL;
        it->second.rewardPrevMonth = it->second.rewardMonth;
        it->second.rewardMonth = csdb::Amount{ 0 };
        ++it;
    }

    auto rewIt = minedEvaluation_.begin();
    while (rewIt != minedEvaluation_.end()) {
        auto delIt = rewIt->second.me.begin();
        while (delIt != rewIt->second.me.end()) {
            delIt->second.rewardPrevMonth = delIt->second.rewardMonth;
            delIt->second.rewardMonth = csdb::Amount{ 0 };
            ++delIt;
        }
        ++rewIt;
    }

    totalMined_.rewardPrevMonth = totalMined_.rewardMonth;
    totalMined_.rewardMonth = csdb::Amount{ 0 };
}

void RoundStat::setNodeStatus(const cs::PublicKey key, bool status) {
    auto it = nodes_.find(key);
    if (it == nodes_.end()) {
        return;
    }
    it->second.nodeOn = status;
}

void RoundStat::fillMinedEvaluation(const cs::PublicKeys& confidants, const std::vector<csdb::Amount>& rew) {
    std::map<cs::PublicKey, csdb::Amount> blockRewardsDistribution;
    //auto confidants = block.confidants();
    //auto realTrusted = cs::Utils::bitsToMask(block.numberTrusted(), block.realTrusted());


    //auto rew = WalletsCache::Updater::getRewardDistribution(block);
    auto rewIt = rew.begin();
    bool rewFlag = rew.size() > 0;

    csdb::Amount totalStake = 0;
    std::vector<csdb::Amount> confidantAndStake;
    int32_t realTrustedNumber = 0;
    const uint8_t kUntrustedMarker = 255;
    for (size_t cfd = 0; cfd < confidants.size(); ++cfd) {
        csdb::Amount nodeConfidantAndStake;
        csdb::Amount nodeConfidantAndFreezenStake;
        csdb::Amount totalNodeStake = 0;
        BlockChain::WalletData wData;
        blockChain_->findWalletData(csdb::Address::from_public_key(confidants[cfd]), wData);
        nodeConfidantAndStake += wData.balance_ * blockChain_->getStakingCoefficient(StakingCoefficient::NoStaking);
        if (wData.delegateSources_ != nullptr && wData.delegateSources_->size() > 0) {
            for (auto& keyAndStake : *(wData.delegateSources_)) {
                for (auto& tm : keyAndStake.second) {
                    if (tm.coeff == StakingCoefficient::NoStaking) {
                        nodeConfidantAndStake += tm.amount * blockChain_->getStakingCoefficient(StakingCoefficient::NoStaking);
                        //csdebug() << "fillMinedEvaluation - simple delegation added: " << tm.amount.to_string();
                    }
                    else {
                        nodeConfidantAndFreezenStake += tm.amount * blockChain_->getStakingCoefficient(tm.coeff);
                        //csdebug() << "fillMinedEvaluation - time delegation added: " << tm.amount.to_string() << " as " << nodeConfidantAndFreezenStake.to_string();
                    }

                }
            }
            totalNodeStake = nodeConfidantAndStake + nodeConfidantAndFreezenStake;
            if (totalNodeStake == csdb::Amount{ 0 }) {
                continue;
            }
            auto rewardPart = *rewIt / totalNodeStake;
            //csdebug() << "setBlockReward - total node stake: " << totalNodeStake.to_string();
            //distributing block reward for each node
            for (auto& keyAndStake : *(wData.delegateSources_)) {
                for (auto& tm : keyAndStake.second) {
                    auto cfdIt = minedEvaluation_.find(confidants[cfd]);
                    if (cfdIt == minedEvaluation_.end()) {
                        minedEvaluation_[confidants[cfd]] = MinedEvaluationDelegator();
                        cfdIt = minedEvaluation_.find(confidants[cfd]);
                    }
                    auto ksIt = cfdIt->second.me.find(keyAndStake.first);
                    if (ksIt == cfdIt->second.me.end()) {
                        cfdIt->second.me[keyAndStake.first] = MinedEvaluation();
                        ksIt = cfdIt->second.me.find(keyAndStake.first);
                    }


                    if (tm.coeff == StakingCoefficient::NoStaking) {
                        ksIt->second.rewardDay += tm.amount * blockChain_->getStakingCoefficient(StakingCoefficient::NoStaking) * rewardPart;
                        ksIt->second.rewardMonth += tm.amount * blockChain_->getStakingCoefficient(StakingCoefficient::NoStaking) * rewardPart;
                        ksIt->second.rewardTotal += tm.amount * blockChain_->getStakingCoefficient(StakingCoefficient::NoStaking) * rewardPart;
                    }
                    else {
                        ksIt->second.rewardDay += tm.amount * blockChain_->getStakingCoefficient(tm.coeff) * rewardPart;
                        ksIt->second.rewardMonth += tm.amount * blockChain_->getStakingCoefficient(tm.coeff) * rewardPart;
                        ksIt->second.rewardTotal += tm.amount * blockChain_->getStakingCoefficient(tm.coeff) * rewardPart;
                    }

                }
            }
        }
        ++rewIt;
    }
}

Bytes NodeStat::to_bytes() {
    Bytes data;
    ODataStream os(data);
    os << nodeOn;
    os << ip;
    os << version;
    os << platform;
    os << timeReg;
    os << timeFirstConsensus;
    os << timeActive;
    os << trustedDay;
    os << trustedMonth;
    os << trustedPrevMonth;
    os << trustedTotal;
    os << failedTrustedDay;
    os << failedTrustedMonth;
    os << failedTrustedPrevMonth;
    os << failedTrustedTotal;
    os << trustedADay;
    os << trustedAMonth;
    os << trustedAPrevMonth;
    os << trustedATotal;
    os << failedTrustedADay;
    os << failedTrustedAMonth;
    os << failedTrustedAPrevMonth;
    os << failedTrustedATotal;
    os << feeDay;
    os << feeMonth;
    os << feePrevMonth;
    os << feeTotal;
    os << rewardDay;
    os << rewardMonth;
    os << rewardPrevMonth;
    os << rewardTotal;
    os << lastConsensus;
    return data;
}

NodeStat NodeStat::from_bytes(Bytes& data) {
    NodeStat res;
    IDataStream is(data.data(), data.size());
    is >> res.nodeOn;
    is >> res.ip;
    is >> res.version;
    is >> res.platform;
    is >> res.timeReg;
    is >> res.timeFirstConsensus;
    is >> res.timeActive;
    is >> res.trustedDay;
    is >> res.trustedMonth;
    is >> res.trustedPrevMonth;
    is >> res.trustedTotal;
    is >> res.failedTrustedDay;
    is >> res.failedTrustedMonth;
    is >> res.failedTrustedPrevMonth;
    is >> res.failedTrustedTotal;
    is >> res.trustedADay;
    is >> res.trustedAMonth;
    is >> res.trustedAPrevMonth;
    is >> res.trustedATotal;
    is >> res.failedTrustedADay;
    is >> res.failedTrustedAMonth;
    is >> res.failedTrustedAPrevMonth;
    is >> res.failedTrustedATotal;
    is >> res.feeDay;
    is >> res.feeMonth;
    is >> res.feePrevMonth;
    is >> res.feeTotal;
    is >> res.rewardDay;
    is >> res.rewardMonth;
    is >> res.rewardPrevMonth;
    is >> res.rewardTotal;
    is >> res.lastConsensus;
    return res;
}

Bytes MinedEvaluationDelegator::to_bytes() {
    Bytes data;
    ODataStream os(data);
    os << me.size();
    auto it = me.begin();
    while (it != me.end()) {
        os << it->first << it->second.to_bytes();
        ++it;
    }
    return data;
}

MinedEvaluationDelegator MinedEvaluationDelegator::from_bytes(Bytes& data) {
    MinedEvaluationDelegator res;
    IDataStream is(data.data(), data.size());
    size_t mSize = 0;
    is >> mSize;
    for (size_t i = 0ULL; i < mSize; ++i) {
        PublicKey pKey;
        Bytes mData;
        is >> pKey >> mData;
        res.me.emplace(pKey, MinedEvaluation::from_bytes(mData));
    }

    return res;
}

Bytes MinedEvaluation::to_bytes() {
    Bytes data;
    ODataStream os(data);
    os << rewardDay;
    os << rewardMonth;
    os << rewardPrevMonth;
    os << rewardTotal;
    return data;
}

MinedEvaluation MinedEvaluation::from_bytes(Bytes& data) {
    MinedEvaluation res;
    IDataStream is(data.data(), data.size());
    is >> res.rewardDay;
    is >> res.rewardMonth;
    is >> res.rewardPrevMonth;
    is >> res.rewardTotal;
    return res;
}


Bytes RoundStat::serialize() {
    Bytes data;
    ODataStream os(data);
    size_t totalBchTransactions_ = totalAcceptedTransactions_;
    os << totalBchTransactions_;

    os << minedEvaluation_.size();
    auto itm = minedEvaluation_.begin();
    while (itm != minedEvaluation_.end()) {
        os << itm->first << itm->second.to_bytes();
        ++itm;
    }
    os << nodes_.size();
    auto itn = nodes_.begin();
    while (itn != nodes_.end()) {
        os << itn->first << itn->second.to_bytes();
        ++itn;
    }
    os << totalMined_.to_bytes();
    return data;
}
void RoundStat::deserialize(Bytes& data) {
    IDataStream is(data.data(), data.size());
    size_t totalBchTransactions_ = 0ULL;
    is >> totalBchTransactions_;
    Bytes dataME;
    size_t me_size;
    is >> me_size;
    for (size_t i = 0ULL; i < me_size; ++i) {
        PublicKey pKey;
        MinedEvaluationDelegator md;
        Bytes data;
        is >> pKey >> data;
        md.from_bytes(data);
        minedEvaluation_.emplace(pKey, MinedEvaluationDelegator::from_bytes(data));
    }

    size_t n_size;
    is >> n_size;
    for (size_t i = 0ULL; i < n_size; ++i) {
        PublicKey pKey;
        Bytes data;
        is >> pKey >> data;
        nodes_.emplace(pKey, NodeStat::from_bytes(data));
    }
    Bytes dataTM;
    is >> dataTM;
    totalMined_ = MinedEvaluation::from_bytes(dataTM);
    totalAcceptedTransactions_ = totalBchTransactions_;
}

void RoundStat::clear() {
    minedEvaluation_.clear();
    nodes_.clear();
    totalMined_ = MinedEvaluation();
    totalAcceptedTransactions_ = 0;
}

void RoundStat::printClassInfo(){
    size_t cnt = 0ULL;
    csdebug() << "Mined:";
    for (auto it : minedEvaluation_) {
        csdebug() << cnt << ". " << cs::Utils::byteStreamToHex(it.first);
        size_t cntt = 0ULL;
        for (auto itt : it.second.me) {
            csdebug() << cntt << ". " << cs::Utils::byteStreamToHex(itt.first) << " " << itt.second.rewardDay.to_string() << " " << itt.second.rewardMonth.to_string() << " " << itt.second.rewardPrevMonth.to_string() << " " << itt.second.rewardTotal.to_string();
            ++cntt;
        }
        ++cnt;
    }
    cnt = 0ULL;
    csdebug() << "Nodes:";
    for (auto it : nodes_) {
        csdebug() << cnt << ". " << cs::Utils::byteStreamToHex(it.first) << ": " << it.second.toString();
        ++cnt;
    }
    csdebug() << "Total mined: " << totalMined_.rewardDay.to_string() << " " << totalMined_.rewardMonth.to_string() << " " << totalMined_.rewardPrevMonth.to_string() << " " << totalMined_.rewardTotal.to_string();

    csdebug() << "Total transactions: " << totalAcceptedTransactions_;
}


std::string NodeStat::toString() {
    std::string res;
    res += std::string(nodeOn ? "Node ON" : "Node OFF");
    res += ", ip: " + ip;
    res += ", version: " + version;
    res += ", platform: " + platform + ",\n";
    res += "timeReg: " + std::to_string(timeReg);
    res += ", timeFirstConsensus: " + std::to_string(timeFirstConsensus);
    res += ", lastConsensus: " + std::to_string(lastConsensus);
    res += ", timeActive: " + std::to_string(timeActive) + "\n";
    res += "trustedDay: " + std::to_string(trustedDay) + ", trustedMonth: " + std::to_string(trustedMonth)
        + ", trustedPrevMonth: " + std::to_string(trustedPrevMonth) + ", trustedTotal: " + std::to_string(trustedTotal) + "\n";;
    res += "failedTrustedDay: " + std::to_string(failedTrustedDay) + ", failedTrustedMonth: " + std::to_string(failedTrustedMonth)
        + ", failedTrustedPrevMonth: " + std::to_string(failedTrustedPrevMonth) + ", failedTrustedTotal: " + std::to_string(failedTrustedTotal) + "\n";;
    res += "trustedADay: " + std::to_string(trustedADay) + ", trustedAMonth: " + std::to_string(trustedAMonth)
        + ", trustedAPrevMonth: " + std::to_string(trustedAPrevMonth) + ", trustedATotal: " + std::to_string(trustedATotal) + "\n";;
    res += "failedTrustedADay: " + std::to_string(failedTrustedADay) + ", failedTrustedAMonth: " + std::to_string(failedTrustedAMonth)
        + ", failedTrustedAPrevMonth: " + std::to_string(failedTrustedAPrevMonth) + ", failedTrustedAPrevMonth: " + std::to_string(failedTrustedAPrevMonth) + "\n";;
    res += "feeDay: " + feeDay.to_string() + ", feeMonth: " + feeMonth.to_string()
        + ", failedTrustedPrevMonth: " + feeMonth.to_string() + ", failedTrustedTotal: " + feeMonth.to_string() + "\n";;
    res += "rewardDay: " + rewardDay.to_string() + ", rewardMonth: " + rewardMonth.to_string()
        + ", rewardPrevMonth: " + rewardPrevMonth.to_string() + ", rewardTotal: " + rewardTotal.to_string() + "\n";;

    return res;
}
void RoundStat::countTrustAndTrx(const csdb::Pool& block) {
    using namespace std::chrono;
    totalAcceptedTransactions_ += block.transactions_count();
    auto confs = block.confidants();
    auto trusted = cs::Utils::bitsToMask(block.numberTrusted(), block.realTrusted());
    const int rTrustedNumber = static_cast<int>(std::count(trusted.begin(), trusted.end(), 0));
    csdb::Amount rCost{ 0 };
    if (block.transactions_count() > 0) {
        for (auto tr : block.transactions()) {
            rCost += tr.counted_fee().to_double();
        }
    }
    auto feePart = (rTrustedNumber != 0) ? rCost / rTrustedNumber : csdb::Amount(0);
    std::string blockTime = block.user_field(BlockChain::kFieldTimestamp).is_valid() ? block.user_field(BlockChain::kFieldTimestamp).value<std::string>(): "0";
    int64_t bTime = static_cast<int64_t>((std::stoll(blockTime.empty() ? "0" : blockTime))/1000);
    const time_t longTime = (time_t)bTime;
    struct tm* structTime = gmtime(&longTime);
    //csdebug() << "Block time: " << blockTime << " == " << longTime << " -> " << structTime->tm_mon << " - " << structTime->tm_mday << " " << structTime->tm_hour << ":" << structTime->tm_min << ":" << structTime->tm_sec;
    if (bTime == 0ULL) {
        return;
    }
    if (block.sequence() < 5ULL) {
        lastMonth_ = structTime->tm_mon;
        lastDay_ = structTime->tm_mday;
    }
    bool dayChange = lastDay_ != structTime->tm_mday;
    bool monthChange = lastMonth_ != structTime->tm_mon;
    lastMonth_ = structTime->tm_mon;
    lastDay_ = structTime->tm_mday;
    if (dayChange) {
        dayChangeProcedure(bTime);
    }
    if (monthChange) {
        monthChangeProcedure();
    }

    auto rew = WalletsCache::Updater::getRewardDistribution(block);
    if (rew.size() > 0) {
        fillMinedEvaluation(block.confidants(), rew);

        for (auto rIt : rew) {
            totalMined_.rewardDay += rIt;
            totalMined_.rewardMonth += rIt;
            totalMined_.rewardTotal += rIt;
        }
    }
    auto rewIt = rew.begin();
    bool rewFlag = rew.size() > 0;
    for (int i = 0; i < trusted.size(); ++i) {
        const auto& key = confs[i];
        if (nodes_.find(key) != nodes_.end()) {
            if (!nodes_[key].nodeOn) {
                nodes_[key].nodeOn = true;
                nodes_[key].timeActive = bTime;
            }
            
            nodes_[key].lastConsensus = bTime;
            if (trusted[i] == 0) {
                if (block.transactions_count() > 0) {
                    nodes_[key].trustedDay += 1;
                    nodes_[key].trustedMonth += 1;
                    nodes_[key].trustedTotal += 1;
                    nodes_[key].feeDay += feePart;
                    nodes_[key].feeMonth += feePart;
                    nodes_[key].feeTotal += feePart;
                }
                nodes_[key].trustedADay += 1;
                nodes_[key].trustedAMonth += 1;
                nodes_[key].trustedATotal += 1;
                if (rewFlag) {
                    nodes_[key].rewardDay += *rewIt;
                    nodes_[key].rewardMonth += *rewIt;
                    nodes_[key].rewardTotal += *rewIt;
                }


            }
            else {
                if (block.transactions_count() > 0) {
                    nodes_[key].failedTrustedDay += 1;
                    nodes_[key].failedTrustedMonth += 1;
                    nodes_[key].failedTrustedTotal += 1;
                }
                nodes_[key].failedTrustedADay += 1;
                nodes_[key].failedTrustedAMonth += 1;
                nodes_[key].failedTrustedATotal += 1;
            }
        }
        else {
            auto nStat = new NodeStat();
            nStat->nodeOn = true;
            int32_t kFieldTimestamp = 0;
            int64_t lts;
            try {
                lts = std::stoll(block.user_field(kFieldTimestamp).value<std::string>());
            }
            catch (...) {
                csdebug() << "Timestamp of block #" << block.sequence() << " can't be read";
                lts = 0LL;
            }
            nStat->timeFirstConsensus = static_cast<uint64_t>(lts);

            emit accountInitiationRequest(nStat->timeReg, key);
            if (trusted[i] == 0) {
                if (block.transactions_count() > 0) {
                    nStat->trustedDay = 1;
                    nStat->trustedMonth = 1;
                    nStat->trustedTotal = 1;
                    nStat->feeDay = feePart;
                    nStat->feeMonth = feePart;
                    nStat->feeTotal = feePart;
                }
                nStat->trustedADay = 1;
                nStat->trustedAMonth = 1;
                nStat->trustedATotal = 1;
                if (rewFlag) {
                    nodes_[key].rewardDay = *rewIt;
                    nodes_[key].rewardMonth = *rewIt;
                    nodes_[key].rewardTotal = *rewIt;
                }

            }
            else {
                if (block.transactions_count() > 0) {
                    nStat->failedTrustedDay = 1;
                    nStat->failedTrustedMonth = 1;
                    nStat->failedTrustedTotal = 1;
                }
                nStat->failedTrustedADay = 1;
                nStat->failedTrustedAMonth = 1;
                nStat->failedTrustedATotal = 1;
            }
            nStat->lastConsensus = block.sequence();
            nodes_.emplace(key, *nStat);
        }
        if (rewFlag) {
            ++rewIt;
        }
    }


    if (rewFlag) {

    }

}



void RoundStat::onReadBlock(const csdb::Pool& block, bool* /*shouldStop*/) {

    countTrustAndTrx(block);
}

void RoundStat::onStoreBlock(const csdb::Pool& block) {
    countTrustAndTrx(block);
}

void RoundStat::onStopReadingFromDb(uint64_t totalTransactions, bool successfulQuickStart) {
    //csdebug() << __func__;
    //if (successfulQuickStart) {
    //    totalAcceptedTransactions_ == totalBlockChainTransactions_;
    //    csdebug() << "Saving " << totalBlockChainTransactions_ << " into total transactions";
    //}
    if (totalAcceptedTransactions_ == totalTransactions){
        totalAcceptedTransactions_ = totalTransactions;
        csdebug() << "All transactions read successfully";
    }
    else{
        cserror() << " The number of counted transactions is different: accepted: " << totalAcceptedTransactions_ << ", in bch: " << totalTransactions;
    }
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
