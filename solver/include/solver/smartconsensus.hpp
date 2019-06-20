#pragma once
#include "callsqueuescheduler.hpp"
#include "consensus.hpp"
#include "stage.hpp"
#include "timeouttracking.hpp"

//#include <csnode/node.hpp>
//#include <solvercore.hpp>
#include <csnode/transactionspacket.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class Node;

namespace cs {
class SolverCore;
class SmartContracts;

class SmartConsensus {
public:
    /*SmartConsensus();*/

    SmartConsensus(/*Node* node*/);

    ~SmartConsensus();

    bool initSmartRound(const cs::TransactionsPacket& pack, uint8_t runCounter, Node* node, SmartContracts* smarts);
    uint8_t calculateSmartsConfNum();
    uint8_t ownSmartsConfidantNumber();

    template <class T>
    bool smartStageEnough(const std::vector<T>& smartStageStorage, const std::string& funcName);

    void startTimer(int st);
    void killTimer();
    void fakeStage(uint8_t confIndex);

    // cs::PublicKey smartAddress();
    // Solver smarts consensus methods
    // void smartStagesStorageClear(size_t cSize);

    void addSmartStageOne(cs::StageOneSmarts& stage, bool send);
    void addSmartStageTwo(cs::StageTwoSmarts& stage, bool send);
    void addSmartStageThree(cs::StageThreeSmarts& stage, bool send);

    static void sendFakeStageOne(Node * pnode, cs::PublicKeys confidants, cs::Byte confidantIndex, uint64_t smartId);
    static void sendFakeStageTwo(Node * pnode, cs::PublicKeys confidants, cs::Byte confidantIndex, uint64_t smartId);

    // void getSmartResult(const cs::TransactionsPacket pack);
    void refreshSmartStagesStorage();
    void processStages();
    std::vector <csdb::Amount> calculateFinalFee(const std::vector <csdb::Amount>& finalFee, size_t realTrustedAmount);

    bool smartStageOneEnough();
    bool smartStageTwoEnough();
    bool smartStageThreeEnough();
    cs::Sequence smartRoundNumber();

    void createFinalTransactionSet(const std::vector<csdb::Amount>& finalFees);
    size_t smartStage3StorageSize();
    void sendFinalTransactionSet();
    bool smartConfidantExist(uint8_t);
    void gotSmartStageRequest(uint8_t msgType, cs::Sequence smartRound, uint32_t startTransaction, uint8_t requesterNumber, uint8_t requiredNumber, const cs::PublicKey& requester);

    void requestSmartStages(int st);
    void requestSmartStagesNeighbors(int st);
    void markSmartOutboundNodes(int st);

    const std::vector<cs::PublicKey>& smartConfidants() const;

    TimeoutTracking timeout_request_stage;
    TimeoutTracking timeout_request_neighbors;
    TimeoutTracking timeout_force_transition;
    int timeoutStageCounter_;

    uint8_t runCounter() const {
        return runCounter_;
    }

    // uint64_t[5 bytes] + uint16_t[2 bytes] + uint8_t[1 byte]
    static inline uint64_t createId(uint64_t seq, uint16_t idx, uint8_t cnt) {
        return (((seq & 0xFFFFFFFFFF) << 24) | (uint64_t(idx) << 8) | static_cast<uint64_t>(cnt));
    }

    // smartRoundNumber[5 bytes] + smartTransaction[2 bytes] + runCounter[1 byte]
    uint64_t id() const {
        return SmartConsensus::createId(smartRoundNumber_, uint16_t(smartTransaction_), runCounter_);
    }

    static inline cs::Sequence blockPart(uint64_t id) {
        return ((id >> 24) & 0x000000FFFFFFFFFF);
    }

    static inline uint32_t transactionPart(uint64_t id) {
        return ((id >> 8) & 0x000000000000FFFF);
    }

    static inline uint32_t runCounterPart(uint64_t id) {
        return (id & 0x00000000000000FF);
    }

private:
    void fake_stage1(uint8_t from);
    void fake_stage2(uint8_t from);

    void init_zero(cs::StageOneSmarts& stage);
    void init_zero(cs::StageTwoSmarts& stage);

    CallsQueueScheduler::CallTag timer_tag_{CallsQueueScheduler::no_tag};
    CallsQueueScheduler::CallTag timer_tag() {
        if (timer_tag_ == CallsQueueScheduler::no_tag) {
            timer_tag_ = id();
        }
        return timer_tag_;
    }

    Node* pnode_;
    SmartContracts* psmarts_;

    std::vector<cs::StageOneSmarts> smartStageOneStorage_;
    std::vector<cs::StageTwoSmarts> smartStageTwoStorage_;
    std::vector<cs::StageThreeSmarts> smartStageThreeStorage_;
    std::vector<cs::StageThreeSmarts> smartStageThreeTempStorage_;
    bool smartStagesStorageRefreshed_ = false;
    std::vector<cs::PublicKey> smartConfidants_;
    uint8_t ownSmartsConfNum_ = cs::ConfidantConsts::InvalidConfidantIndex;
    cs::TransactionsPacket currentSmartTransactionPack_;
    cs::TransactionsPacket finalSmartTransactionPack_;
    std::vector <csdb::Transaction> tmpNewStates_;
    cs::StageOneSmarts st1;
    cs::StageTwoSmarts st2;
    cs::StageThreeSmarts st3;
    std::vector<int> smartUntrusted;
    std::vector<csdb::Pool::SmartSignature> solverSmartSignatures_;
    cs::Sequence smartRoundNumber_;
    uint32_t smartTransaction_;
    uint8_t runCounter_;
    bool trustedChanged_ = false;
    bool smartStageThreeSent_ = false;
    std::vector<cs::Bytes> smartStageOneMessage_;
    std::vector<cs::Bytes> smartStageTwoMessage_;
    std::vector<cs::Bytes> smartStageThreeMessage_;

    std::vector<cs::Stage> smartStageTemporary_;
    cs::Bytes smartConsensusMask;
    csdb::Transaction finalStateTransaction_;
};

}  // namespace cs
