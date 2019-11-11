#include <consensus.hpp>
#include <smartcontracts.hpp>
#include <solvercontext.hpp>
#include <states/trustedstage1state.hpp>

#include <csdb/amount.hpp>
#include <csnode/blockchain.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/itervalidator.hpp>
#include <csnode/transactionspacket.hpp>
#include <csnode/walletscache.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <csnode/datastream.hpp>
#include <cscrypto/cscrypto.hpp>
#include <string>

namespace cs {
void TrustedStage1State::on(SolverContext& context) {
    if (!pValidator_) {
        pValidator_ = std::make_unique<IterValidator>(context.wallets());
    }
    likeMineHashes = 0;
    differentHashes = 0;
    differKeys.clear();
    DefaultStateBehavior::on(context);
    context.init_zero(stage);
    stage.sender = context.own_conf_number();
    enough_hashes = false;
    transactions_checked = false;
    min_time_expired = false;

    SolverContext* pctx = &context;
    auto dt = Consensus::T_min_stage1;
    csdebug() << name() << ": start track min time " << dt << " ms to get hashes";

    cs::Timer::singleShot(dt, cs::RunPolicy::CallQueuePolicy, [this, pctx]() {
        csdebug() << name() << ": min time to get hashes is expired, may proceed to the next state";
        min_time_expired = true;
        if (transactions_checked && enough_hashes) {
            csdebug() << name() << ": transactions & hashes ready, so proceed to the next state now";
            pctx->complete_stage1();
        }
    });

    // min_time_tracking.start(
    //  context.scheduler(), dt,
    //  [this, pctx](){
    //    csdebug() << name() << ": min time to get hashes is expired, may proceed to the next state";
    //    min_time_expired = true;
    //    if (transactions_checked && enough_hashes) {
    //      csdebug() << name() << ": transactions & hashes ready, so proceed to the next state now";
    //      pctx->complete_stage1();
    //    }
    //  },
    //  true /*replace if exists*/
    //);
}

void TrustedStage1State::finalizeStage(SolverContext& context) {
    
    //if(context.own_conf_number() == 1 && cs::Conveyer::instance().currentRoundNumber() > 10) {
    //    stage.roundTimeStamp = std::to_string(std::stoll(cs::Utils::currentTimestamp()) + 10000); 
    //} 
    //else {
    uint64_t lastTimeStamp = std::atoll(context.blockchain().getLastTimeStamp().c_str());
    uint64_t currentTimeStamp = std::atoll(cs::Utils::currentTimestamp().c_str());
    if (currentTimeStamp < lastTimeStamp) {
        currentTimeStamp = lastTimeStamp + 1;
    }
    stage.roundTimeStamp = std::to_string(currentTimeStamp);
        /*}*/
    stage.toBytes();
    stage.messageHash = cscrypto::calculateHash(stage.message.data(), stage.message.size());
    cs::Bytes messageToSign;
    messageToSign.reserve(sizeof(cs::RoundNumber) + sizeof(uint8_t) + sizeof(cs::Hash));
    cs::DataStream signStream(messageToSign);
    signStream << cs::Conveyer::instance().currentRoundNumber();
    signStream << context.subRound();
    signStream << stage.messageHash;
    stage.signature = cscrypto::generateSignature(context.private_key(), messageToSign.data(), messageToSign.size());
}

void TrustedStage1State::off(SolverContext& context) {
    // if (min_time_tracking.cancel()) {
    //  csdebug() << name() << ": cancel track min time to get hashes";
    //}
    csdebug() << name() << ": --> stage-1 [" << static_cast<int>(stage.sender) << "]";
    if (min_time_expired && transactions_checked && enough_hashes) {
        finalizeStage(context);
        context.add_stage1(stage, true);
    }
}

Result TrustedStage1State::onSyncTransactions(SolverContext& context, cs::RoundNumber round) {
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    if (round < conveyer.currentRoundNumber()) {
        cserror() << name() << ": cannot handle transactions from old round " << round;
        return Result::Ignore;
    }

    csdebug() << name() << ": -------> STARTING CONSENSUS #" << conveyer.currentRoundNumber() << " <------- ";
    auto data = conveyer.createPacket(round);

    if (!data.has_value()) {
        cserror() << name() << ": error while prepare consensus to build vector, maybe method called before sync completed?";
        return Result::Ignore;
    }

    // bindings
    auto&& [packet, smartContractPackets] = std::move(data).value();

    csdebug() << name() << ": packet of " << packet.transactionsCount() << " transactions in" << typeid(conveyer).name();
    if (!smartContractPackets.empty()) {
        csdebug() << name() << ": smart contract packets count " << smartContractPackets.size();
        if (!smartContractPackets.empty()) {
            for (const auto& p : smartContractPackets) {
                csdetails() << name() << ": packet hash " << p.hash().toString();
            }
        }
    }

    // review & validate transactions
    stage.hash = build_vector(context, packet, smartContractPackets);

    {
        std::unique_lock<cs::SharedMutex> lock = conveyer.lock();
        size_t trxCounter = 0;
        size_t preliminaryBlockSize = 0;
        const cs::RoundTable& roundTable = conveyer.currentRoundTable();
        bool finishFlag = false;
        bool continueFlag = false;
        size_t tSize = 0;
        for (const auto& element : conveyer.transactionsPacketTable()) {
            const cs::PacketsHashes& hashes = roundTable.hashes;
            continueFlag = false;
            if (std::find(hashes.cbegin(), hashes.cend(), element.first) == hashes.cend()) {
                
                if (stage.hashesCandidates.size() > Consensus::MaxStageOneHashes) {
                    csdebug() << name() << ": stage-1 transactions " << trxCounter;
                    break;
                }

                trxCounter += element.second.transactionsCount();
                if (trxCounter + element.second.transactionsCount() > Consensus::MaxStageOneTransactions) {
                    csdebug() << name() << ": stage-1 transactions " << trxCounter - element.second.transactionsCount();
                    break;
                }

                for (auto& it : element.second.transactions()) {
                    tSize = it.to_byte_stream().size();
                    preliminaryBlockSize += tSize;
                    if (preliminaryBlockSize > Consensus::MaxPreliminaryBlockSize) {
                        finishFlag = true;
                        csdebug() << name() << ": stage-1 transactions " << trxCounter - element.second.transactionsCount();
                        break;
                    }
                    if (tSize > Consensus::MaxTransactionSize) {
                        continueFlag = true;
                        csdebug() << name() << ": stage-1 transactions " << trxCounter - element.second.transactionsCount();
                        break;
                    }

                }
                if (finishFlag) {
                    csdebug() << name() << ": stage-1 transactions " << trxCounter - element.second.transactionsCount();
                    break;
                }
                if (continueFlag) {
                    continue;
                }

                stage.hashesCandidates.push_back(element.first);
            }
        }
    }

    transactions_checked = true;
    bool other_conditions = enough_hashes && min_time_expired;
    return (other_conditions ? Result::Finish : Result::Ignore);
}

Result TrustedStage1State::onHash(SolverContext& context, const csdb::PoolHash& pool_hash, const cs::PublicKey& sender) {
    csdb::PoolHash lastHash = context.blockchain().getLastHash();
    csdb::PoolHash spoiledHash = context.spoileHash(lastHash, sender);
    std::string stagesPlot;
    std::string hString = std::string(name()) + ": <-- hash from " + std::string(context.sender_description(sender));
    if (spoiledHash == pool_hash) {
        // get node status for useful logging
        if (cs::Conveyer::instance().isConfidantExists(sender)) {
            ++likeMineHashes;
        }
        
        csdebug() << hString<< ": hash is OK";
        if (std::find(stage.trustedCandidates.cbegin(), stage.trustedCandidates.cend(), sender) == stage.trustedCandidates.cend()) {
            stage.trustedCandidates.push_back(sender);
        }
        //TODO: print hashMask string
        if (stage.trustedCandidates.size() >= Consensus::MinTrustedNodes) {
            if (likeMineHashes >= cs::Conveyer::instance().confidantsCount() / 2) {
                enough_hashes = true;
                bool other_conditions = transactions_checked && min_time_expired;
                return (other_conditions ? Result::Finish : Result::Ignore);
            }
            else if(differentHashes >= cs::Conveyer::instance().confidantsCount() / 2){
                context.askTrustedRound(cs::Conveyer::instance().currentRoundNumber(), differKeys);
            }
        }
    }
    else {
        csdebug() << hString << ": DOES NOT MATCH my value " << lastHash.to_string();
        //TODO: print hashMask string
        if (cs::Conveyer::instance().isConfidantExists(sender)) {
            ++differentHashes;
            differKeys.push_back(sender);
        }
        else {
            context.sendHashReply(std::move(pool_hash), sender);
        }
        if (differentHashes >= cs::Conveyer::instance().confidantsCount() / 2) {
            context.askTrustedRound(cs::Conveyer::instance().currentRoundNumber(), differKeys);
        }

    }

    return Result::Ignore;
}

cs::Hash TrustedStage1State::build_vector(SolverContext& context, cs::TransactionsPacket& packet, cs::Packets& smartsPackets) {
    const std::size_t transactionsCount = packet.transactionsCount();

    cs::Characteristic characteristic;

    if (transactionsCount > 0) {
        characteristic = pValidator_->formCharacteristic(context, packet.transactions(), smartsPackets);
    }

    if (characteristic.mask.size() != transactionsCount) {
        cserror() << name() << ": characteristic mask size is not equal to transactions count in build_vector()";
    }

    cs::Conveyer& conveyer = cs::Conveyer::instance();
    conveyer.setCharacteristic(characteristic, conveyer.currentRoundNumber());

    return formHashFromCharacteristic(characteristic);
}

cs::Hash TrustedStage1State::formHashFromCharacteristic(const cs::Characteristic& characteristic) {
    cs::Hash hash;

    if (characteristic.mask.empty()) {
        cs::Conveyer& conveyer = cs::Conveyer::instance();
        auto round = conveyer.currentRoundNumber();
        hash = cscrypto::calculateHash(reinterpret_cast<cs::Byte*>(&round), sizeof(cs::RoundNumber));
    }
    else {
        hash = cscrypto::calculateHash(characteristic.mask.data(), characteristic.mask.size());
    }

    csdebug() << name() << ": generated hash: " << cs::Utils::byteStreamToHex(hash.data(), hash.size());
    return hash;
}
}  // namespace cs
