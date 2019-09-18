#include <csnode/blockvalidatorplugins.hpp>

#include <string>
#include <algorithm>
#include <set>

#include <csdb/pool.hpp>
#include <csnode/blockchain.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/common.hpp>
#include <csnode/walletsstate.hpp>
#include <csnode/walletscache.hpp>
#include <csdb/amount_commission.hpp>
#include <csdb/pool.hpp>
#include <cscrypto/cscrypto.hpp>
#include <smartcontracts.hpp>

#include <base58.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace {
const char* kLogPrefix = "BlockValidator: ";
const cs::Sequence kGapBtwNeighbourBlocks = 1;
const csdb::user_field_id_t kTimeStampUserFieldNum = 0;
const uint8_t kBlockVerToSwitchCountedFees = 0;
} // namespace

namespace cs {

const cs::SmartContracts* ValidationPlugin::getSmartContracts() const {
    const auto ptr = blockValidator_.node_.getSolver();
    if (ptr == nullptr) {
        return nullptr;
    }
    return &ptr->smart_contracts();
}

ValidationPlugin::ErrorType
SmartStateValidator::validateBlock(const csdb::Pool& block) {

    // CreditsNet has got known issues due to deprecated behavior in ancient times, so:
    if (getBlockChain().uuid() == 11024959585341937636ULL) {
        if (block.sequence() < 90728) {
            // skip unable-to-validate contracts
            return ErrorType::noError;
        }

        if (block.sequence() < 3302505) {
            // skip invalid contracts until first valid
            return ErrorType::noError;
        }
    }

    const auto& transactions = block.transactions();
    for (const auto& t : transactions) {
        if (SmartContracts::is_new_state(t) && !checkNewState(t)) {
            cserror() << kLogPrefix << "error occured during new state check in block "
                      << block.sequence();
            return ErrorType::error;
        }
    }
    return ErrorType::noError;
}

bool SmartStateValidator::checkNewState(const csdb::Transaction& t) {
    SmartContractRef ref = t.user_field(trx_uf::new_state::RefStart);
    if (!ref.is_valid()) {
        cserror() << kLogPrefix << "ref to start trx is not valid";
        return false;
    }
    auto block = getBlockChain().loadBlock(ref.sequence);
    if (!block.is_valid()) {
        cserror() << "load block with init trx failed";
        return false;
    }
    auto connectorPtr = getNode().getConnector();
    if (connectorPtr == nullptr) {
        cserror() << kLogPrefix << "unavailable connector ptr";
        return false;
    }
    auto executorPtr = connectorPtr->apiExecHandler();
    if (executorPtr == nullptr) {
        cserror() << kLogPrefix << "unavailable executor ptr";
        return false;
    }
    if (ref.transaction >= block.transactions().size()) {
        cserror() << kLogPrefix << "incorrect reference to start transaction";
        return false;
    }
    executor::Executor::ExecuteTransactionInfo info{};
    info.transaction = block.transactions().at(ref.transaction);
    info.sequence = ref.sequence;
    info.feeLimit = csdb::Amount(info.transaction.max_fee().to_double()); // specify limit as SmartContracts::execute() does
    info.convention = executor::Executor::MethodNameConvention::Default;
    if (!is_smart(info.transaction)) {
        // to specify Payable or PayableLegacy convention call correctly we require access to smarts object
        info.convention = executor::Executor::MethodNameConvention::PayableLegacy;
        // the most frequent fast test
        //auto item = known_contracts.find(absolute_address(transaction.target()));
        //if (item != known_contracts.end()) {
        //    StateItem& state = item->second;
        //    if (state.payable == PayableStatus::Implemented) {
        //        info.convention = executor::Executor::MethodNameConvention::PayableLegacy;
        //    }
        //    else if (state.payable == PayableStatus::ImplementedVer1) {
        //        info.convention = executor::Executor::MethodNameConvention::Payable;
        //    }
        //}
    }
    auto opt_result = executorPtr->getExecutor().reexecuteContract(info, std::string{} /*no force new_state required*/);
    if (!opt_result.has_value()) {
        cserror() << kLogPrefix << "execution of transaction failed";
        return false;
    }
    const auto& result = opt_result.value();
    if (result.smartsRes.empty()) {
        cserror() << kLogPrefix << "execution result is incorrect, it must not be empty";
        return false;
    }
    auto& main_result = result.smartsRes.front();

    if (!cs::SmartContracts::is_state_updated(t)) {
        csdb::Address abs_addr = getBlockChain().getAddressByType(t.target(), BlockChain::AddressType::PublicKey);
        if (main_result.states.count(abs_addr) > 0) {
            if (!main_result.states.at(abs_addr).empty()) {
                csdebug() << kLogPrefix << "new state in trx is empty, but real new state is not";
            }
        }
        return true;
    }
    else {
        csdb::Address abs_addr = getBlockChain().getAddressByType(t.target(), BlockChain::AddressType::PublicKey);
        if (main_result.states.count(abs_addr) == 0) {
            csdebug() << kLogPrefix << "real new state in is empty, but new state in trx is not";
        }
        else {
            std::string newState = cs::SmartContracts::get_contract_state(getBlockChain(), t.target());
            if (newState != main_result.states.at(abs_addr)) {
                cserror() << kLogPrefix << "new state of trx in blockchain doesn't match real new state";
                return false;
            }
        }
    }
    return true;
}

ValidationPlugin::ErrorType HashValidator::validateBlock(const csdb::Pool& block) {
  auto prevHash = block.previous_hash();
  auto& prevBlock = getPrevBlock();
  auto data = prevBlock.to_binary();
  auto countedPrevHash = csdb::PoolHash::calc_from_data(cs::Bytes(data.data(),
                                                          data.data() +
                                                          prevBlock.hashingLength()));
  if (prevHash != countedPrevHash) {
    csfatal() << kLogPrefix << ": prev pool's (" << prevBlock.sequence()
              << ") hash != real prev pool's hash";
    return ErrorType::fatalError;      
  }
  return ErrorType::noError;
}

ValidationPlugin::ErrorType BlockNumValidator::validateBlock(const csdb::Pool& block) {
  auto& prevBlock = getPrevBlock();
  if (block.sequence() - prevBlock.sequence() != kGapBtwNeighbourBlocks) {
    cserror() << kLogPrefix << "Current block's sequence is " << block.sequence()
              << ", previous block sequence is " << prevBlock.sequence();
    return ErrorType::error;
  }
  return ErrorType::noError;
}

ValidationPlugin::ErrorType TimestampValidator::validateBlock(const csdb::Pool& block) {
  auto& prevBlock = getPrevBlock();

  auto prevBlockTimestampUf = prevBlock.user_field(kTimeStampUserFieldNum);
  if (!prevBlockTimestampUf.is_valid()) {
    cswarning() << kLogPrefix << "Block with sequence " << prevBlock.sequence() << " has no timestamp";
    return ErrorType::warning;
  }
  auto currentBlockTimestampUf = block.user_field(kTimeStampUserFieldNum);
  if (!currentBlockTimestampUf.is_valid()) {
    cswarning() << kLogPrefix << "Block with sequence " << block.sequence() << " has no timestamp";
    return ErrorType::warning;
  }

  auto prevBlockTimestamp = std::stoll(prevBlockTimestampUf.value<std::string>());
  auto currentBlockTimestamp = std::stoll(currentBlockTimestampUf.value<std::string>());
  if (currentBlockTimestamp < prevBlockTimestamp) {
    cswarning() << kLogPrefix << "Block with sequence " << block.sequence()
                << " has timestamp " << currentBlockTimestamp
                << " less than " << prevBlockTimestamp
                << " in block with sequence " << prevBlock.sequence();
    return ErrorType::warning;
  }
  return ErrorType::noError;
}

ValidationPlugin::ErrorType BlockSignaturesValidator::validateBlock(const csdb::Pool& block) {
  uint64_t realTrustedMask = block.realTrusted();
#ifdef _MSC_VER
  size_t numOfRealTrusted = static_cast<decltype(numOfRealTrusted)>(__popcnt64(realTrustedMask));
#else
  size_t numOfRealTrusted = static_cast<decltype(numOfRealTrusted)>(__builtin_popcountl(realTrustedMask));
#endif

  auto signatures = block.signatures();
  if (signatures.size() != numOfRealTrusted) {
    cserror() << kLogPrefix << "in block " << block.sequence()
              << " num of signatures (" << signatures.size()
              << ") != num of real trusted (" << numOfRealTrusted << ")";
    return ErrorType::error;
  }

  auto confidants = block.confidants();
  const size_t maxTrustedNum = sizeof(realTrustedMask) * 8;
  if (confidants.size() > maxTrustedNum) {
    cserror() << kLogPrefix << "in block " << block.sequence()
              << " num of confidants " << confidants.size()
              << " is greated than max bits in realTrustedMask";
    return ErrorType::error;
  }

  size_t checkingSignature = 0;
  auto signedData = cscrypto::calculateHash(block.to_binary().data(), block.hashingLength());
  for (size_t i = 0; i < confidants.size(); ++i) {
    if (realTrustedMask & (1ull << i)) {
      if (!cscrypto::verifySignature(signatures[checkingSignature],
                                     confidants[i],
                                     signedData.data(),
                                     cscrypto::kHashSize)) {
        cserror() << kLogPrefix << "block " << block.sequence()
                  << " has invalid signatures";
        return ErrorType::error;
      }
      ++checkingSignature;
    }
  }

  return ErrorType::noError;
}

ValidationPlugin::ErrorType SmartSourceSignaturesValidator::validateBlock(const csdb::Pool& block) {
  const auto& transactions = block.transactions();
  const auto& smartSignatures = block.smartSignatures();

  if (smartSignatures.empty()) {
    if (containsNewState(transactions)) {
        cserror() << kLogPrefix << "no smart signatures in block "
                  << block.sequence() << ", which contains new state";
        return ErrorType::error;
    }
    return ErrorType::noError;
  }

  bool switchCountedFees = block.version() == kBlockVerToSwitchCountedFees;
  auto smartPacks = grepNewStatesPacks(transactions, switchCountedFees);

  if (!checkSignatures(smartSignatures, smartPacks)) {
    return ErrorType::error;
  }

  return ErrorType::noError;
}

bool SmartSourceSignaturesValidator::checkSignatures(const SmartSignatures& sigs,
                                                     const Packets& smartPacks) {
  if (sigs.size() != smartPacks.size()) {
    cserror() << kLogPrefix << "q-ty of smart signatures != q-ty of real smart packets"; 
    return false;
  }

  for (const auto& pack : smartPacks) {
    auto it = std::find_if(sigs.begin(), sigs.end(),
                           [&pack] (const csdb::Pool::SmartSignature& s) {
                           return pack.transactions()[0].source().public_key() == s.smartKey; });

    if (it == sigs.end()) {
      cserror() << kLogPrefix << "no smart signatures for new state with key "
                << pack.transactions()[0].source().to_string();
      return false;
    }

    auto initPool = getBlockChain().loadBlock(it->smartConsensusPool);
    const auto& confidants = initPool.confidants();
    const auto& smartSignatures = it->signatures;
    for (const auto& s : smartSignatures) {
      if (s.first >= confidants.size()) {
        cserror() << kLogPrefix << "smart signature validation: no conf with index "
                  << s.first << " in init pool with sequence " << initPool.sequence();
        return false;
      }
      if (!cscrypto::verifySignature(s.second, confidants[s.first], pack.hash().toBinary().data(), cscrypto::kHashSize)) {
        cserror() << kLogPrefix << "incorrect signature of smart "
                  << pack.transactions()[0].source().to_string() << " of confidant " << s.first
                  << " from init pool with sequence " << initPool.sequence();
        return false;
      }
    }
  }

  return true;
}

inline bool SmartSourceSignaturesValidator::containsNewState(const Transactions& trxs) {
  for (const auto& t : trxs) {
    if (SmartContracts::is_new_state(t)) {
      return true;
    }
  }
  return false;
}

Packets SmartSourceSignaturesValidator::grepNewStatesPacks(const Transactions& trxs, bool switchFees) {
  Packets res;
  for (size_t i = 0; i < trxs.size(); ++i) {
    if (SmartContracts::is_new_state(trxs[i])) {
      cs::TransactionsPacket pack;
      pack.addTransaction(switchFees ? switchCountedFee(trxs[i]) : trxs[i]);
      std::for_each(trxs.begin() + i + 1, trxs.end(),
          [&] (const csdb::Transaction& t) {
            if (t.source() == trxs[i].source()) {
              pack.addTransaction(switchFees ? switchCountedFee(t) : t);
            }
          });
      pack.makeHash();
      res.push_back(pack);
    }
  }
  return res;
}

csdb::Transaction SmartSourceSignaturesValidator::switchCountedFee(const csdb::Transaction& t) {
  csdb::Transaction initTrx = cs::SmartContracts::get_transaction(getBlockChain(), t);
  if (!initTrx.is_valid()) {
    cserror() << kLogPrefix << " no init transaction for smart state transaction in blockchain";
    return t;
  }
  csdb::Transaction res(t.innerID(), t.source(), t.target(), t.currency(), t.amount(), t.max_fee(),
                        initTrx.counted_fee(), t.signature());
  auto ufIds = t.user_field_ids();
  for (const auto& id : ufIds) {
    res.add_user_field(id, t.user_field(id));
  }
  return res;
}

ValidationPlugin::ErrorType BalanceChecker::validateBlock(const csdb::Pool&) {
  const auto& prevBlock = getPrevBlock();
  if (prevBlock.transactions().empty()) {
    return ErrorType::noError;
  }

  const auto& trxs = prevBlock.transactions();
  auto wallets = getWallets();
  wallets->updateFromSource();
  for (const auto& t : trxs) {
    const auto& wallState = wallets->getData(t.source());
    if (wallState.balance_ < zeroBalance_) {
      cserror() << kLogPrefix << "error detected in pool " << prevBlock.sequence()
                << ", wall address " << t.source().to_string()
                << " has balance " << wallState.balance_.to_double();
      return ErrorType::error;
    }
  }

  return ErrorType::noError;
}

ValidationPlugin::ErrorType TransactionsChecker::validateBlock(const csdb::Pool& block) {
  const auto& trxs = block.transactions();
  std::set<csdb::Address> newStates;
  for (const auto& t : trxs) {
    if (SmartContracts::is_new_state(t)) {
      // already checked by another plugin
      newStates.insert(t.source());
      continue;
    }

    auto it = std::find(newStates.begin(), newStates.end(), t.source());
    if (it != newStates.end()) {
      continue;
    }

    if (!checkSignature(t)) {
      cserror() << kLogPrefix << " in pool " << block.sequence()
                << " transaction from " << t.source().to_string()
                << ", with innerID " << t.innerID()
                << " has incorrect signature";
      return ErrorType::error;
    }
  }
  return ErrorType::noError;
}

bool TransactionsChecker::checkSignature(const csdb::Transaction& t) {
  if (t.source().is_wallet_id()) {
    const auto& bc = getBlockChain();
    auto pub = bc.getAddressByType(t.source(), BlockChain::AddressType::PublicKey);
    return t.verify_signature(pub.public_key());
  } else {
    return t.verify_signature(t.source().public_key());
  }
}

//
// AccountChecker
// 

AccountBalanceChecker::AccountBalanceChecker(BlockValidator& bv, const char* base58_key)
    : ValidationPlugin(bv)
{
    cs::Bytes account_key;
    if (DecodeBase58(std::string(base58_key), account_key)) {
        abs_addr = csdb::Address::from_public_key(account_key);
    }
}

ValidationPlugin::ErrorType AccountBalanceChecker::validateBlock(const csdb::Pool& block) {

    constexpr double epsilon = 0.000001;
    const cs::Sequence seq = block.sequence();

    // test execution timeouts
    if (!inprogress.empty()) {
        while (!inprogress.empty()) {
            size_t idx = inprogress.front();
            if (idx <= all_transactions.size()) {
                auto& item = all_transactions[idx];
                if (seq > item.seq && seq - item.seq < Consensus::MaxRoundsCancelContract) {
                    // no timeout yet
                    break;
                }
                // roll back canceled call
                double return_sum = item.t.amount().to_double();
                balance += return_sum;
                incomes.push_back(Income{ idx, return_sum, std::list<ExtraFee>{} });
                // erase item in progress
                for (auto it = inprogress.cbegin(); it != inprogress.cend(); ++it) {
                    if (*it == idx) {
                        inprogress.erase(it);
                        break;
                    }
                }
            }
        }
    }

    size_t t_idx = 0;
    // stores every new_state contract's address to detect emitted transactions
    bool possible_emitted = false;
    csdb::Address emitted_src_abs_addr{};
    csdb::Address emitted_src_opt_addr{};

    //bool possible_error = false;
    constexpr size_t StopOn = 99;

    for (const auto& t : block.transactions()) {

        size_t cnt_all_transactions = all_transactions.size();

        // get opt_addr if it has not got yet
        if (!opt_addr.is_wallet_id()) {
            if (t.target() == abs_addr || t.source() == abs_addr) {
                for (const auto& w : block.newWallets()) {
                    if (w.addressId_.trxInd_ == t_idx) {
                        if (w.addressId_.addressType_ == csdb::Pool::NewWalletInfo::AddressIsSource) {
                            if (t.source() == abs_addr) {
                                opt_addr = csdb::Address::from_wallet_id(w.walletId_);
                            }
                            break;
                        }
                        else if (w.addressId_.addressType_ == csdb::Pool::NewWalletInfo::AddressIsTarget) {
                            if (t.target() == abs_addr) {
                                opt_addr = csdb::Address::from_wallet_id(w.walletId_);
                            }
                            break;
                        }
                        else {
                            cswarning() << kLogPrefix << "failed to get opt_addr for account";
                        }
                    }
                }
            }
        }

        double new_balance = balance;
        std::list<ExtraFee> extra_fee;
        bool call_to_contract = false;

        // process smart contract
        if (cs::SmartContracts::is_smart_contract(t)) {
            possible_emitted = false;
            call_to_contract = cs::SmartContracts::is_executable(t);
            if (call_to_contract) {
                if (!iam_contract) {
                    if (t.target() == opt_addr || t.target() == abs_addr) {
                        if (cs::SmartContracts::is_deploy(t)) {
                            iam_contract = true;
                        }
                        else {
                        }
                    }
                }
            }
            else if (cs::SmartContracts::is_new_state(t)) {
                if (!iam_contract) {

                    csdb::UserField fld = t.user_field(cs::trx_uf::new_state::RefStart);
                    if (fld.is_valid()) {
                        SmartContractRef ref_start(fld);
                        csdb::Transaction starter = cs::SmartContracts::get_transaction(getBlockChain(), ref_start);
                        if (starter.is_valid()) {
                            if (starter.source() == opt_addr || starter.source() == abs_addr) {
                                if (!cs::SmartContracts::is_state_updated(t)) {
                                    // rollback execution
                                    for (auto it = inprogress.cbegin(); it != inprogress.cend(); ++it) {
                                        size_t idx = *it;
                                        if (idx < all_transactions.size()) {
                                            const auto& item = all_transactions[idx];
                                            if (item.seq == ref_start.sequence && item.idx == ref_start.transaction) {
                                                // roll back canceled call
                                                double return_sum = item.t.amount().to_double();
                                                new_balance += return_sum;
                                                // erase item in progress
                                                inprogress.erase(it);
                                                break;
                                            }
                                        }
                                    }
                                }
                                else {
                                    // my account had to pay
                                    new_balance -= t.counted_fee().to_double();
                                    possible_emitted = true;
                                    emitted_src_abs_addr = t.source();
                                    // assume WalletsIds have already updated
                                    csdb::internal::WalletId wid;
                                    if (getBlockChain().findWalletId(emitted_src_abs_addr, wid)) {
                                        emitted_src_opt_addr = csdb::Address::from_wallet_id(wid);
                                    }
                                    all_transactions.emplace_back(Transaction{ block.sequence(), t_idx, t.clone() });
                                    erase_inprogress(ref_start);
                                }
                            
                                // execution fee is always paid
                                fld = t.user_field(cs::trx_uf::new_state::Fee);
                                if (fld.is_valid()) {
                                    csdb::Amount fee = fld.value<csdb::Amount>();
                                    extra_fee.emplace_back(ExtraFee{ fee.to_double(), "exec fee" });
                                }
                            }
                        }
                    }

                }
            }
        }
        else if (possible_emitted) {
            // test emitted transactions, initer have to pay fee for them 
            if (t.source() == emitted_src_abs_addr || t.source() == emitted_src_opt_addr) {
                extra_fee.emplace_back(ExtraFee{t.counted_fee().to_double(), "emitted fee"});
                // if contract send transaction not to my account and I should pay fee, store it
                if (t.target() != abs_addr && t.target() != opt_addr) {
                    /* Transaction& tmp =*/ all_transactions.emplace_back(Transaction{ block.sequence(), t_idx, t.clone() });
                }
            }
            else {
                possible_emitted = false;
            }
        }
        else {
            // test is my account replenishes payable contract
            if (t.source() == abs_addr || t.source() == opt_addr) {
                const cs::SmartContracts* psmarts = getSmartContracts();
                if (psmarts != nullptr) {
                    if (psmarts->is_known_smart_contract(t.target())) {
                        call_to_contract = true;
                    }
                }
            }
        }
         
        // process as target
        if (t.target() == opt_addr || t.target() == abs_addr) {
           /* Transaction& tmp =*/ all_transactions.emplace_back(Transaction{ block.sequence(), t_idx, t.clone() });
            double sum = t.amount().to_double();
            new_balance = balance + sum;
            if (call_to_contract) {
                inprogress.push_back(all_transactions.size() - 1);
            }
        }

        // process as source
        if (t.source() == opt_addr || t.source() == abs_addr) {
            /*Transaction& tmp =*/ all_transactions.emplace_back(Transaction{ block.sequence(), t_idx, t.clone() });
            double sum = t.amount().to_double();
            new_balance = balance - sum;
            if (!iam_contract) {
                // contract does not pay for emitted transaction(s)
                new_balance -= t.counted_fee().to_double();
            }
            if (call_to_contract) {
                //possible_error = true;
                inprogress.push_back(all_transactions.size() - 1);
            }
        }

        if (!extra_fee.empty()) {
            for (const auto& e : extra_fee) {
                new_balance -= e.fee;
            }
        }


        if (fabs(new_balance - balance) > DBL_EPSILON) {

            // update balance, fix invalid operations
            if (new_balance < -DBL_EPSILON) {
                invalid_ops.emplace_back(InvalidOperation{ all_transactions.size() - 1, balance, new_balance - balance, new_balance, extra_fee });
            }

            if (new_balance > balance) {
                incomes.push_back(Income{ all_transactions.size() - 1, new_balance - balance, extra_fee });
            }
            else {
                expenses.push_back(Expense{ all_transactions.size() - 1, new_balance - balance, extra_fee });
            }
            balance = new_balance;

            csdebug() << kLogPrefix << "balance has changed by " << FormatRef(t.id().pool_seq(), t.id().index()) << " to new value " << balance;
        }

        ++t_idx;
    }

    double wallet_balance = get_wallet_balance();
    double new_balance_error = wallet_balance - balance;
    if (inprogress.empty() && fabs(new_balance_error - balance_error) > epsilon) {
        cserror() << kLogPrefix << "balance " << balance << " mismatch wallet_balance " << wallet_balance << " after " << WithDelimiters(block.sequence());
        balance_error = new_balance_error;
    }

    return ValidationPlugin::ErrorType::noError;
}

double AccountBalanceChecker::get_wallet_balance() {
    BlockChain::WalletData data;
    if (getBlockChain().findWalletData(abs_addr, data)) {
        return data.balance_.to_double();
    }
    return 0;
}

void AccountBalanceChecker::erase_inprogress(const cs::SmartContractRef& ref) {
    size_t idx = 0;
    for (const auto& item: all_transactions) {
        if (item.seq == ref.sequence && item.idx == ref.transaction) {
            const auto it_erase = std::find(inprogress.cbegin(), inprogress.cend(), idx);
            if (it_erase != inprogress.cend()) {
                inprogress.erase(it_erase);
                break;
            }
        }
        ++idx;
    }

}

//double AccountBalanceChecker::rollback_execution(size_t idx) {
//    double sum = 0;
//    if (idx <= all_transactions.size()) {
//        auto& item = all_transactions[idx];
//        // roll back canceled call
//        double return_sum = item.t.amount().to_double();
//        sum += return_sum;
//        incomes.push_back(Income{ idx, return_sum, std::list<ExtraFee>{} });
//        // erase item in progress
//        for (auto it = inprogress.cbegin(); it != inprogress.cend(); ++it) {
//            if (*it == idx) {
//                inprogress.erase(it);
//                break;
//            }
//        }
//    }
//    return sum;
//}

} // namespace cs
