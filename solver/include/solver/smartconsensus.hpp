#pragma once
#include "callsqueuescheduler.hpp"
#include "timeouttracking.hpp"
#include "consensus.hpp"
#include "stage.hpp"

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

  class SmartConsensus{
    public:

      /*SmartConsensus();*/

      SmartConsensus(/*Node* node*/);

      ~SmartConsensus();

      bool initSmartRound(const cs::TransactionsPacket& pack, Node* node, SmartContracts* smarts);
      uint8_t calculateSmartsConfNum();
      uint8_t ownSmartsConfidantNumber();

      template<class T>
      bool smartStageEnough(const std::vector<T>& smartStageStorage, const std::string& funcName);

      void startTimer(int st);
      void killTimer(int st);
      void fakeStage(uint8_t confIndex);

      //cs::PublicKey smartAddress();
      //Solver smarts consensus methods
      //void smartStagesStorageClear(size_t cSize);

      void addSmartStageOne(cs::StageOneSmarts& stage, bool send);
      void addSmartStageTwo(cs::StageTwoSmarts& stage, bool send);
      void addSmartStageThree(cs::StageThreeSmarts& stage, bool send);
      //void getSmartResult(const cs::TransactionsPacket pack);
      void refreshSmartStagesStorage();
      void processStages();
      csdb::Amount calculateFinalFee(const csdb::Amount& finalFee, size_t realTrustedAmount);


      bool smartStageOneEnough();
      bool smartStageTwoEnough();
      bool smartStageThreeEnough();
      cs::Sequence smartRoundNumber();

      void createFinalTransactionSet(const csdb::Amount finalFee);
      size_t smartStage3StorageSize();
      void sendFinalTransactionSet();
      bool smartConfidantExist(uint8_t);
      void gotSmartStageRequest(uint8_t msgType, cs::Sequence smartRound, uint32_t startTransaction,
        uint8_t requesterNumber, uint8_t requiredNumber, const cs::PublicKey& requester);

      void requestSmartStages(int st);
      void requestSmartStagesNeighbors(int st);
      void markSmartOutboundNodes(int st);

      const std::vector<cs::PublicKey>& smartConfidants() const;

      TimeoutTracking timeout_request_stage;
      TimeoutTracking timeout_request_neighbors;
      TimeoutTracking timeout_force_transition;
    private:
      void fake_stage1(uint8_t from);
      void fake_stage2(uint8_t from);

      void init_zero(cs::StageOneSmarts & stage);
      void init_zero(cs::StageTwoSmarts & stage);

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
      csdb::Transaction tmpNewState_;
      cs::StageOneSmarts st1;
      cs::StageTwoSmarts st2;
      cs::StageThreeSmarts st3;
      std::vector <int> smartUntrusted;
      std::vector <csdb::Pool::SmartSignature> solverSmartSignatures_;
      cs::Sequence smartRoundNumber_;
      uint32_t smartTransaction_;
      bool trustedChanged_ = false;
      bool smartStageThreeSent_ = false;
      cs::Hash  zeroHash;
      cs::Signature  zeroSignature;
      std::vector<cs::Bytes> smartStageOneMessage_;
      std::vector<cs::Bytes> smartStageTwoMessage_;
      std::vector<cs::Bytes> smartStageThreeMessage_;

      std::vector<cs::Stage> smartStageTemporary_;
  };
}
