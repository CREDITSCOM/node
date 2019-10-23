#pragma once

#include "inodestate.hpp"
#include "solvercore.hpp"

#include <csdb/pool.hpp>

class CallsQueueScheduler;
class Node;
class BlockChain;

namespace cs {
class TransactionsPacket;
}

namespace cs {
class SolverCore;
using KeyType = cs::Bytes;

/**
 * @enum    Role
 *
 * @brief   Values that represent roles, repeats analog defined in node.hpp
 */

enum class Role {
    Normal,
    Trusted,
    Writer
};

/**
 * @class   SolverContext
 *
 * @brief   A solver context.
 *
 *          "Интерфейсный" класс для обращений из классов состояний к ядру солвера, определяет
 *          подмножество вызовов солвера, которые доступны из классов состояний, д. б.
 *          достаточным, но не избыточным одновременно.
 *
 * @author  aae
 * @date    03.10.2018
 */

class SolverContext {
public:
    SolverContext() = delete;

    explicit SolverContext(SolverCore& core)
    : core(core) {
    }

    /**
     * @fn  void SolverContext::request_role(Role role)
     *
     * @brief   Request core to activate one of predefined role (@see Role). Obviously it is achieved
     *          by switching current state. Request may be "ignored" depending on current state and
     *          content of transition table.
     *
     * @author  Alexander Avramenko
     * @date    15.10.2018
     *
     * @param   role    The role requested.
     */

    void request_role(Role role) {
        switch (role) {
            case Role::Normal:
                core.handleTransitions(SolverCore::Event::SetNormal);
                break;
            case Role::Trusted:
                core.handleTransitions(SolverCore::Event::SetTrusted);
                break;
            case Role::Writer:
                core.handleTransitions(SolverCore::Event::SetWriter);
                break;
        }
    }

    void complete_stage1() {
        core.handleTransitions(SolverCore::Event::Hashes);  // SolverCore::Event::Transactions may be used as well
    }

    void complete_stage2() {
        core.handleTransitions(SolverCore::Event::Stage1Enough);
    }

    void complete_stage3() {
        core.handleTransitions(SolverCore::Event::Stage2Enough);
    }

    void fail_stage3() {
        core.handleTransitions(SolverCore::Event::FailConsensus);
    }

    void complete_post_stage() {
        core.handleTransitions(SolverCore::Event::Stage3Enough);
    }

    void back_to_stage3() {
        core.handleTransitions(SolverCore::Event::Stage3NonComplete);
    }
    /**
     * @fn  NodeLevel SolverContext::level() const;
     *
     * @brief   Gets the current node role as set in last round table
     *
     * @author  Alexander Avramenko
     * @date    15.10.2018
     *
     * @return  A node role set in last round table.
     */

    Role role() const;

    /**
     * @fn  void SolverContext::spawn_next_round(const std::vector<PublicKey>& nodes);
     *
     * @brief   Spawn request to next round.
     *
     * @author  aae
     * @date    03.10.2018
     *
     * @param   nodes   The nodes.
     */

    bool addSignaturesToLastBlock(Signatures&& blockSignatures);

    void spawn_next_round(cs::StageThree& st3);

    void spawn_first_round() {
        // TODO: implement method
    }

    void sendRoundTable();

    void next_trusted_candidates(const std::vector<cs::PublicKey>& nodes, const std::vector<cs::TransactionsPacketHash>& hashes = std::vector<cs::TransactionsPacketHash>());

    // Fast access methods, may be removed at the end

    /**
     * @fn	BlockChain& SolverContext::blockchain() const;
     *
     * @brief	Gets the blockchain instance
     *
     * @author	User
     * @date	09.10.2018
     *
     * @return	A reference to a BlockChain instance.
     */

    BlockChain& blockchain() const;

    /**
     * @fn    cs::WalletsState& SolverContext::wallets() const
     *
     * @brief Gets the wallets service instance
     *
     * @author    Alexander Avramenko
     * @date  07.12.2018
     *
     * @return    A reference to a cs::WalletsState instance.
     */

    cs::WalletsState& wallets() const {
        return *core.pws;
    }

    /**
     * @fn    cs::SmartContracts& SolverContext::smart_contracts() const
     *
     * @brief Smart contracts
     *
     * @author    Alexander Avramenko
     * @date  10.01.2019
     *
     * @return    A reference to the cs::SmartContracts.
     */

    cs::SmartContracts& smart_contracts() const {
        return *core.psmarts;
    }

    /**
     * @fn  CallsQueueScheduler& SolverContext::scheduler() const;
     *
     * @brief   Gets the scheduler instance.
     *
     * @author  aae
     * @date    03.10.2018
     *
     * @return  A reference to a CallsQueueScheduler.
     *
     * ### remarks  Aae, 30.09.2018.
     */

    CallsQueueScheduler& scheduler() const {
        return core.scheduler;
    }

    // Access to common state properties.

    /**
     * @fn  const KeyType& SolverContext::public_key() const
     *
     * @brief   Public key.
     *
     * @author  Alexander Avramenko
     * @date    10.10.2018
     *
     * @return  A reference to a const KeyType public key.
     *
     * ### remarks  Aae, 30.09.2018.
     */

    const cs::PublicKey& public_key() const {
        return core.public_key;
    }

    /**
     * @fn  const KeyType& SolverContext::private_key() const;
     *
     * @brief   Private key.
     *
     * @author  aae
     * @date    03.10.2018
     *
     * @return  A reference to a const KeyType private key.
     *
     * ### remarks  Aae, 30.09.2018.
     */

    const cs::PrivateKey& private_key() const {
        return core.private_key;
    }

    std::string sender_description(const cs::PublicKey& sender_id);

    csdb::PoolHash spoileHash(const csdb::PoolHash& hashToSpoil, const cs::PublicKey& pKey);

    uint64_t lastTimeStamp();

    void sendHashReply(const csdb::PoolHash& hash, const cs::PublicKey& respondent);

    void add_stage1(cs::StageOne& stage, bool send);

    void add_stage2(cs::StageTwo& stage, bool send);

    void add_stage3(cs::StageThree& stage);

    const std::vector<cs::StageOne>& stage1_data() const {
        return core.stageOneStorage;
    }

    const std::vector<cs::StageTwo>& stage2_data() const {
        return core.stageTwoStorage;
    }

    const std::vector<cs::StageThree>& stage3_data() const {
        return core.stageThreeStorage;
    }

    const std::vector<cs::StageThree>& final_stage3_data() const {
        return core.trueStageThreeStorage;
    }

    const cs::StageOne* stage1(uint8_t sender) const {
        return core.find_stage1(sender);
    }

    size_t trueStagesThree() const {
        return core.trueStagesThree();
    }

    bool realTrustedChanged() const {
        return core.realTrustedChanged();
    }

    void realTrustedChangedSet(bool val) const {
        return core.realTrustedChangedSet(val);
    }

    void realTrustedSetValue(cs::Byte position, cs::Byte value) {
        csdebug() << __func__;
        core.realTrustedSetValue(position, value);
    }

    void realTrustedSet(cs::Bytes realTrusted) {
        csdebug() << __func__;
        core.realTrustedSet(realTrusted);
    }

    cs::Bytes getRealTrusted() {
        return core.getRealTrusted();
    }

    bool checkNodeCache(cs::PublicKey sender) const {
        return core.checkNodeCache(sender);
    }

    size_t stagesThree() const {
        return core.stagesThree();
    }

    const cs::StageTwo* stage2(uint8_t sender) const {
        return core.find_stage2(sender);
    }

    const cs::StageThree* stage3(uint8_t sender) const {
        return core.find_stage3(sender);
    }

    void request_stage1(uint8_t from, uint8_t required);

    void request_stage2(uint8_t from, uint8_t required);

    void request_stage3(uint8_t from, uint8_t required);

    void init_zero(cs::StageOne& stage) {
        stage.sender = cs::InvalidSender;
        stage.hash.fill(0);
        stage.messageHash.fill(0);
        stage.signature.fill(0);
        stage.hashesCandidates.clear();
        stage.trustedCandidates.clear();
        stage.roundTimeStamp.clear();
    }

    void init_zero(cs::StageTwo& stage) {
        stage.sender = cs::InvalidSender;
        stage.signature.fill(0);
        size_t cnt = cnt_trusted();
        stage.hashes.resize(cnt, Zero::hash);
        stage.signatures.resize(cnt, Zero::signature);
    }

    void fake_stage1(uint8_t from) {
        if (core.find_stage1(from) == nullptr) {
            csdebug() << "SolverCore: make stage-1 [" << static_cast<int>(from) << "] as silent";
            cs::StageOne fake;
            init_zero(fake);
            fake.sender = from;
            core.gotStageOne(fake);
        }
    }

    void fake_stage2(uint8_t from) {
        if (core.find_stage2(from) == nullptr) {
            csdebug() << "SolverCore: make stage-2 [" << static_cast<int>(from) << "] as silent";
            cs::StageTwo fake;
            init_zero(fake);
            fake.sender = from;
            core.gotStageTwo(fake);
        }
    }

    void fake_stage3(uint8_t from) {
        if (core.find_stage3(from) == nullptr) {
            cs::StageThree fake;
            fake.sender = from;
            fake.iteration = core.getCurrentStage3Iteration();
            core.gotStageThree(fake, false);
        }
    }

    void mark_untrusted(uint8_t sender) {
        if (sender < Consensus::MaxTrustedNodes) {
            if (core.markUntrusted[sender] < std::numeric_limits<uint8_t>::max()) {
                ++(core.markUntrusted[sender]);
            }
        }
    }

    uint8_t untrusted_value(uint8_t sender) const {
        if (sender < Consensus::MaxTrustedNodes) {
            return (core.markUntrusted[sender]);
        }
        return 0;
    }

    /**
     * @fn  uint32_t SolverContext::subRound() const;
     *
     * @brief   Gets the current subround.
     *
     * @author  dc
     * @date    26.10.2018
     *
     * @return  uint8_t.
     *
     * ### remarks  ???
     */

    uint8_t subRound() const {
        return core.subRound();
    }

    /**
     * @fn  uint8_t SolverContext::own_conf_number() const;
     *
     * @brief   Gets the own number among confidant (trusted) nodes.
     *
     * @author  aae
     * @date    03.10.2018
     *
     * @return  An uint8_t.
     *
     * ### remarks  Aae, 30.09.2018.
     */

    uint8_t own_conf_number() const;

    /**
     * @fn  size_t SolverContext::cnt_trusted() const;
     *
     * @brief   Gets count of trusted nodes in current round.
     *
     * @author  aae
     * @date    03.10.2018
     *
     * @return  The total number of trusted.
     *
     * ### remarks  Aae, 30.09.2018.
     */

    size_t cnt_trusted() const;

    /**
     * @fn  const std::vector<PublicKey>& SolverContext::trusted() const;
     *
     * @brief   Gets the trusted
     *
     * @author  Alexander Avramenko
     * @date    15.10.2018
     *
     * @return  A reference to a const std::vector&lt;PublicKey&gt;
     *
     */

    size_t cnt_real_trusted() const;

    const std::vector<cs::PublicKey>& trusted() const;

    /**
     * @fn  const uint8_t* SolverContext::last_block_hash();
     *
     * @brief   Last block hash
     *
     * @author  Alexander Avramenko
     * @date    24.10.2018
     *
     * @return  Null if it fails, else a pointer to a const uint8_t.
     */

    cs::Bytes last_block_hash() const;

    /**
     * @fn  void SolverContext::request_round_table() const;
     *
     * @brief   Request round table
     *
     *
     * @author  Alexander Avramenko
     * @date    15.10.2018
     */

    void request_round_table() const;

    /**
     * @fn  void SolverContext::add(const csdb::Transaction& tr);
     *
     * @brief   Adds transaction to inner list
     *
     * @author  aae
     * @date    03.10.2018
     *
     * @param   tr  The tr to add.
     *
     * ### remarks  Aae, 30.09.2018.
     */

    void add(const csdb::Transaction& tr) {
        core.send_wallet_transaction(tr);
    }

    /**
     * @fn  csdb::Address SolverContext::address_genesis() const
     *
     * @brief   Address genesis
     *
     * @author  Alexander Avramenko
     * @date    10.10.2018
     *
     * @return  The csdb::Address.
     */

    csdb::Address address_genesis() const {
        return core.addr_genesis;
    }

    /**
     * @fn  csdb::Address SolverContext::address_start() const
     *
     * @brief   Address start
     *
     * @author  Alexander Avramenko
     * @date    10.10.2018
     *
     * @return  The csdb::Address.
     */

    csdb::Address address_start() const {
        return core.addr_start;
    }

    /**
     * @fn	csdb::Address SolverContext::optimize(const csdb::Address& address) const;
     *
     * @brief	Optimizes the given address. Tries to get wallet id from blockchain, otherwise return dicrect address
     *
     * @author	User
     * @date	09.10.2018
     *
     * @param	address	The address to optimize.
     *
     * @return	The csdb::Address optimized with id if possible
     */

    csdb::Address optimize(const csdb::Address& address) const;

    /**
     * @fn  void SolverContext::send_hash(const cs::Hash& hash, const cs::PublicKey& target);
     *
     * @brief   Sends a hash to a target
     *
     * @author  Alexander Avramenko
     * @date    15.10.2018
     *
     * @param   hash    The hash.
     * @param   target  Target for the.
     */

    void send_hash(const cs::Hash& hash, const cs::PublicKey& target);

    /**
     * @fn  bool SolverContext::test_trusted_idx(uint8_t idx, const cs::PublicKey& sender);
     *
     * @brief   test conformance of node index to public key.
     *
     * @author  Alexander Avramenko
     * @date    31.10.2018
     *
     * @param   idx     Zero-based index of the.
     * @param   sender  The sender.
     *
     * @return  True if the test passes, false if the test fails.
     */

    bool test_trusted_idx(uint8_t idx, const cs::PublicKey& sender);

    /**
     * @fn  bool SolverContext::transaction_still_in_pool(int64_t inner_id) const
     *
     * @brief   Tests if transaction with inner_id passed still in pool (not sent yet)
     *
     * @author  Alexander Avramenko
     * @date    31.10.2018
     *
     * @param   inner_id    Identifier for the inner.
     *
     * @return  True if it succeeds, false if it fails.
     */

    bool transaction_still_in_pool(int64_t inner_id) const;
    void request_round_info(uint8_t respondent1, uint8_t respondent2);

    using RefExecution = std::pair<cs::Sequence, uint32_t>;
    void send_rejected_smarts(const std::vector<RefExecution>& reject_list);

private:
  SolverCore& core;
};

}  // namespace cs
