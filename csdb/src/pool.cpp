#include "csdb/pool.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

#include <lz4.h>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include "csdb/csdb.hpp"

#include "binary_streams.hpp"
#include "csdb/internal/shared_data_ptr_implementation.hpp"
#include "csdb/internal/utils.hpp"
#include "priv_crypto.hpp"
#include "transaction_p.hpp"

namespace csdb {

class PoolHash::priv : public ::csdb::internal::shared_data {
public:
    cs::Bytes value;
    DEFAULT_PRIV_CLONE();
};
SHARED_DATA_CLASS_IMPLEMENTATION(PoolHash)

bool PoolHash::is_empty() const noexcept {
    return d->value.empty();
}

size_t PoolHash::size() const noexcept {
    return d->value.size();
}

std::string PoolHash::to_string() const noexcept {
    return internal::to_hex(d->value);
}

cs::Bytes PoolHash::to_binary() const noexcept {
    return d->value;
}

PoolHash PoolHash::from_binary(cs::Bytes&& data) {
    const size_t sz = data.size();
    PoolHash res;
    if ((0 == sz) || (::csdb::priv::crypto::hash_size == sz)) {
        res.d->value = std::move(data);
    }
    return res;
}

bool PoolHash::operator==(const PoolHash& other) const noexcept {
    return (d == other.d) || (d->value == other.d->value);
}

bool PoolHash::operator<(const PoolHash& other) const noexcept {
    return (d != other.d) && (d->value < other.d->value);
}

PoolHash PoolHash::from_string(const ::std::string& str) {
    const cs::Bytes hash = ::csdb::internal::from_hex(str);
    const size_t sz = hash.size();
    PoolHash res;
    if ((0 == sz) || (::csdb::priv::crypto::hash_size == sz)) {
        res.d->value = hash;
    }
    return res;
}

PoolHash PoolHash::calc_from_data(const cs::Bytes& data) {
    PoolHash res;
    res.d->value = ::csdb::priv::crypto::calc_hash(data);
    return res;
}

void PoolHash::put(::csdb::priv::obstream& os) const {
    size_t size = d->value.size();
    os.put(static_cast<uint8_t>(size));
    if (!d->value.empty()) {
        os.put((const void*)d->value.data(), size);
    }
}

bool PoolHash::get(::csdb::priv::ibstream& is) {
    uint8_t size;
    if (!is.get(size)) {
        return false;
    }
    if (size == 0) {
        d->value.clear();
        return true;
    }
    if (size != cscrypto::kHashSize || is.size() < cscrypto::kHashSize) {
        return false;
    }
    if (d->value.size() != cscrypto::kHashSize) {
        d->value.resize(cscrypto::kHashSize);
    }
    return is.get(d->value.data(), cscrypto::kHashSize);
}

class Pool::priv : public ::csdb::internal::shared_data {
    priv()
    : ::csdb::internal::shared_data() {
    }

    priv(PoolHash previous_hash, cs::Sequence sequence, ::csdb::Storage::WeakPtr storage)
    : is_valid_(true)
    , previous_hash_(std::move(previous_hash))
    , sequence_(sequence)
    , storage_(std::move(storage)) {
    }

    void put(::csdb::priv::obstream& os, bool doHash) const {
        os.put(version_);
        os.put(previous_hash_);
        os.put(sequence_);

        os.put(user_fields_);
        os.put(roundCost_);

        os.put(static_cast<uint32_t>(transactions_.size()));
        for (const auto& it : transactions_) {
            os.put(it);
        }

        os.put(static_cast<uint32_t>(newWallets_.size()));
        for (const auto& wall : newWallets_) {
            os.put(wall);
        }

        os.put(numberTrusted_);
        os.put(realTrusted_);

        // TODO: ensure confidants.size() <= numTrusted_
        for (const auto& it : confidants_) {
            os.put(it);
        }

        os.put(numberConfirmations_);
        os.put(roundConfirmationMask_);
        for (const auto& it : roundConfirmations_) {
            os.put(it);
        }

        if (doHash) {
            return;
        }
        const_cast<size_t&>(hashingLength_) = os.buffer().size();
        os.put(hashingLength_);

        for (const auto& it : signatures_) {
            os.put(it);
        }

        os.put(static_cast<uint8_t>(smartSignatures_.size()));
        for (const auto& it : smartSignatures_) {
            os.put(it.smartKey);
            os.put(it.smartConsensusPool);
            os.put(static_cast<uint8_t>(it.signatures.size()));
            for (const auto& itt : it.signatures) {
                os.put(itt.first);
                os.put(itt.second);
            }
        }
    }

    void put_for_sig(::csdb::priv::obstream& os) const {
        // not used now
        os.put(static_cast<uint8_t>(0));  // version
        os.put(previous_hash_);
        os.put(sequence_);

        os.put(user_fields_);
        os.put(roundCost_);

        os.put(static_cast<uint32_t>(transactions_.size()));
        for (const auto& it : transactions_) {
            os.put(it);
        }

        os.put(static_cast<uint32_t>(newWallets_.size()));
        for (const auto& wall : newWallets_) {
            os.put(wall);
        }

        os.put(static_cast<uint8_t>(confidants_.size()));
        for (const auto& it : confidants_) {
            os.put(it);
        }

        os.put(realTrusted_);
    }

    bool get_meta(::csdb::priv::ibstream& is, size_t& cnt) {
        if (!is.get(version_)) {
            csmeta(cswarning) << "get version is failed";
            return false;
        }

        if (!is.get(previous_hash_)) {
            csmeta(cswarning) << "get previous hash is failed";
            return false;
        }

        if (!is.get(sequence_)) {
            csmeta(cswarning) << "get sequence is failed";
            return false;
        }

        if (!is.get(user_fields_)) {
            csmeta(cswarning) << "get user fields is failed";
            return false;
        }

        if (!is.get(roundCost_)) {
            csmeta(cswarning) << "get round cost is failed";
            return false;
        }

        if (!is.get(transactionsCount_)) {
            csmeta(cswarning) << "get cnt is failed";
            return false;
        }
        cnt = transactionsCount_;
        is_valid_ = true;

        return true;
    }

    void updateHash() {
        const auto begin = binary_representation_.data();
        const auto end = begin + hashingLength_;
        hash_ = PoolHash::calc_from_data(cs::Bytes(begin, end));
    }

    void updateHash(const cs::Bytes& data) {
        hash_ = PoolHash::calc_from_data(data);
    }

    bool getTransactions(::csdb::priv::ibstream& is, size_t cnt) {
        transactions_.clear();
        transactions_.reserve(cnt);
        for (size_t i = 0; i < cnt; ++i) {
            Transaction tran;
            if (!is.get(tran)) {
                return false;
            }
            transactions_.emplace_back(tran);
        }
        return true;
    }

    bool getConfidants(::csdb::priv::ibstream& is) {
        confidants_.clear();
        confidants_.reserve(numberTrusted_);
        for (uint8_t i = 0; i < numberTrusted_; ++i) {
            cs::PublicKey conf;
            if (!is.get(conf)) {
                return false;
            }
            confidants_.emplace_back(conf);
        }
        return true;
    }

    bool getSignatures(::csdb::priv::ibstream& is) {
#ifdef _MSC_VER
        uint8_t cnt = (uint8_t)__popcnt64(realTrusted_);
#else
        uint8_t cnt = __builtin_popcountl(realTrusted_);
#endif

        signatures_.clear();
        signatures_.reserve(static_cast<size_t>(cnt));
        for (uint8_t i = 0; i < cnt; ++i) {
            cs::Signature sig;
            if (!is.get(sig)) {
                return false;
            }

            signatures_.push_back(sig);
        }
        return true;
    }

    bool getTrustedConfirmation(::csdb::priv::ibstream& is) {
        if (!is.get(numberConfirmations_)) {
            return false;
        }

        if (!is.get(roundConfirmationMask_)) {
            return false;
        }

#ifdef _MSC_VER
        uint8_t cnt = (uint8_t)__popcnt64(roundConfirmationMask_);
#else
        uint8_t cnt = __builtin_popcountl(roundConfirmationMask_);
#endif

        roundConfirmations_.clear();
        roundConfirmations_.reserve(static_cast<size_t>(cnt));
        for (size_t i = 0; i < cnt; ++i) {
            cs::Signature sig;

            if (!is.get(sig)) {
                return false;
            }

            roundConfirmations_.push_back(sig);
        }
        return true;
    }

    bool getSmartSignatures(::csdb::priv::ibstream& is) {
        uint8_t cnt = 0;
        if (!is.get(cnt)) {
            return false;
        }

        smartSignatures_.clear();
        smartSignatures_.reserve(cnt);

        for (uint8_t smarts = 0; smarts < cnt; ++smarts) {
            SmartSignature sSig;
            sSig.smartConsensusPool = 0;
            sSig.smartKey.fill(0);
            sSig.signatures.clear();

            if (!is.get(sSig.smartKey)) {
                return false;
            }

            if (!is.get(sSig.smartConsensusPool)) {
                return false;
            }

            uint8_t confCount = 0;
            if (!is.get(confCount)) {
                return false;
            }
            for (uint8_t i = 0; i < confCount; i++) {
                cs::Byte b;
                cs::Signature sig;
                if (!is.get(b)) {
                    return false;
                }
                if (!is.get(sig)) {
                    return false;
                }
                sSig.signatures.emplace_back(std::make_pair(b, sig));
            }

            smartSignatures_.emplace_back(sSig);
        }
        return true;
    }

    bool getNewWallets(::csdb::priv::ibstream& is) {
        uint32_t cnt = 0;
        if (!is.get(cnt)) {
            return false;
        }

        newWallets_.clear();
        newWallets_.reserve(cnt);
        for (uint32_t i = 0; i < cnt; ++i) {
            NewWalletInfo wall;
            if (!is.get(wall)) {
                return false;
            }
            newWallets_.emplace_back(wall);
        }
        return true;
    }

    bool get(::csdb::priv::ibstream& is) {
        size_t cnt;

        if (!get_meta(is, cnt)) {
            csmeta(cswarning) << "get meta is failed";
            return false;
        }

        if (!getTransactions(is, cnt)) {
            csmeta(cswarning) << "get transactions is failed";
            return false;
        }

        if (!getNewWallets(is)) {
            csmeta(cswarning) << "get new wallets is failed";
            return false;
        }

        if (!is.get(numberTrusted_)) {
            csmeta(cswarning) << "get number trusted is failed";
            return false;
        }

        if (!is.get(realTrusted_)) {
            csmeta(cswarning) << "get real trusted is failed";
            return false;
        }

        if (!getConfidants(is)) {
            csmeta(cswarning) << "get confidants is failed";
            return false;
        }

        if (!getTrustedConfirmation(is)) {
            csmeta(cswarning) << "get confirmations is failed";
            return false;
        }

        if (!is.get(hashingLength_)) {
            csmeta(cswarning) << "get hashing length is failed";
            return false;
        }

        if (!getSignatures(is)) {
            csmeta(cswarning) << "get signatures is failed";
            return false;
        }

        if (!getSmartSignatures(is)) {
            csmeta(cswarning) << "get smart signatures is failed";
            return false;
        }
        is_valid_ = true;
        if (is.size() > 0) {
            cserror() << "Pool::get(): inconsistent binary pool";
        }
        return true;
    }

    void compose() {
        if (!is_valid_) {
            binary_representation_.clear();
            hash_ = PoolHash();
            return;
        }

        update_binary_representation();
        update_transactions();
    }

    void update_binary_representation() {
        ::csdb::priv::obstream os;
        put(os, false);
        update_binary_representation(std::move(const_cast<cs::Bytes&>(os.buffer())));
    }

    void update_binary_representation(cs::Bytes&& bytes) {
        binary_representation_ = std::move(bytes);
    }

    void update_transactions() {
        read_only_ = true;

        updateHash();

        for (size_t idx = 0; idx < transactions_.size(); ++idx) {
            transactions_[idx].d->_update_id(hash_, idx);
        }
    }

    Storage get_storage(Storage candidate) {
        if (candidate.isOpen()) {
            return candidate;
        }

        candidate = Storage(storage_);
        if (candidate.isOpen()) {
            return candidate;
        }

        return ::csdb::defaultStorage();
    }

    priv clone() const {
        priv result;

        result.is_valid_ = is_valid_;
        result.version_ = version_;
        result.read_only_ = read_only_;
        result.hash_ = hash_.clone();
        result.previous_hash_ = previous_hash_.clone();
        result.sequence_ = sequence_;
        result.confidants_ = confidants_;
        result.hashingLength_ = hashingLength_;
        result.roundCost_ = roundCost_;

        result.transactions_.reserve(transactions_.size());
        for (auto& t : transactions_) {
            result.transactions_.push_back(t.clone());
        }

        result.transactionsCount_ = transactionsCount_;
        result.newWallets_ = newWallets_;

        for (auto& uf : user_fields_) {
            result.user_fields_[uf.first] = uf.second.clone();
        }

        result.signatures_ = signatures_;
        result.smartSignatures_ = smartSignatures_;
        result.roundConfirmations_ = roundConfirmations_;
        result.realTrusted_ = realTrusted_;
        result.roundConfirmationMask_ = roundConfirmationMask_;
        result.binary_representation_ = binary_representation_;
        result.numberTrusted_ = numberTrusted_;
        result.numberConfirmations_ = numberConfirmations_;

        result.storage_ = storage_;

        return result;
    }

    bool is_valid_ = false;
    bool read_only_ = false;
    uint8_t version_ = 0;
    PoolHash hash_;
    PoolHash previous_hash_;
    cs::Sequence sequence_{};
    std::vector<cs::PublicKey> confidants_;
    ::std::vector<Transaction> transactions_;
    uint32_t transactionsCount_ = 0;
    NewWallets newWallets_;
    ::std::map<::csdb::user_field_id_t, ::csdb::UserField> user_fields_;
    uint8_t numberTrusted_ = 0;
    uint64_t realTrusted_ = 0;
    uint8_t numberConfirmations_ = 0;
    uint64_t roundConfirmationMask_ = 0;
    size_t hashingLength_ = 0;
    csdb::Amount roundCost_;
    std::vector<cs::Signature> signatures_;
    std::vector<cs::Signature> roundConfirmations_;
    ::std::vector<csdb::Pool::SmartSignature> smartSignatures_;
    cs::Bytes binary_representation_;
    ::csdb::Storage::WeakPtr storage_;

    static cs::PublicKey zero_writer_public_key_;
    friend class Pool;
};
SHARED_DATA_CLASS_IMPLEMENTATION(Pool)

/*static*/
cs::PublicKey csdb::Pool::priv::zero_writer_public_key_;

Pool::Pool(PoolHash previous_hash, cs::Sequence sequence, const Storage& storage)
: d(new priv(std::move(previous_hash), sequence, storage.weak_ptr())) {
}

bool Pool::is_valid() const noexcept {
    return d->is_valid_;
}

bool Pool::is_read_only() const noexcept {
    return d->read_only_;
}

uint8_t Pool::version() const noexcept {
    return d->version_;
}

PoolHash Pool::hash() const noexcept {
    if (d->hash_.is_empty()) {
        const_cast<Pool*>(this)->d->updateHash();
    }

    return d->hash_;
}

PoolHash Pool::previous_hash() const noexcept {
    return d->previous_hash_;
}

size_t Pool::hashingLength() const noexcept {
    return d->hashingLength_;
}

Storage Pool::storage() const noexcept {
    return Storage(d->storage_);
}

Transaction Pool::transaction(size_t index) const {
    return (d->transactions_.size() > index) ? d->transactions_[index] : Transaction{};
}

uint8_t Pool::numberTrusted() const noexcept {
    return d->numberTrusted_;
}

uint64_t Pool::realTrusted() const noexcept {
    return d->realTrusted_;
}

uint64_t Pool::roundConfirmationMask() const noexcept {
    return d->roundConfirmationMask_;
}

uint8_t Pool::numberConfirmations() const noexcept {
    return d->numberConfirmations_;
}

const std::vector<cs::Signature>& Pool::roundConfirmations() const noexcept {
    return d->roundConfirmations_;
}

Transaction Pool::transaction(TransactionID id) const {
    if ((!d->is_valid_) || (!d->read_only_) || (!id.is_valid()) || (id.pool_hash() != d->hash_) || (d->transactions_.size() <= id.d->index_)) {
        return Transaction{};
    }
    return d->transactions_[id.d->index_];
}

Transaction Pool::get_last_by_source(const Address& source) const noexcept {
    const auto data = d.constData();

    if ((!data->is_valid_)) {
        return Transaction{};
    }

    auto it_rend = data->transactions_.rend();
    for (auto it = data->transactions_.rbegin(); it != it_rend; ++it) {
        const auto& t = *it;

        if (t.source() == source) {
            return t;
        }
    }

    return Transaction{};
}

Transaction Pool::get_last_by_target(const Address& target) const noexcept {
    const auto data = d.constData();

    if ((!data->is_valid_)) {
        return Transaction{};
    }

    auto it_rend = data->transactions_.rend();
    for (auto it = data->transactions_.rbegin(); it != it_rend; ++it) {
        const auto t = *it;

        if (t.target() == target) {
            return t;
        }
    }

    return Transaction{};
}

bool Pool::add_transaction(Transaction transaction
#ifdef CSDB_UNIT_TEST
                           ,
                           bool skip_check
#endif
) {
    if (d.constData()->read_only_) {
        return false;
    }

    if (!transaction.is_valid()) {
        return false;
    }

#ifdef CSDB_UNIT_TEST
    if (!skip_check) {
#endif
        /// \todo Add transaction checking.
#ifdef CSDB_UNIT_TEST
    }
#endif

    d->transactions_.push_back(Transaction(new Transaction::priv(*(transaction.d.constData()))));
    ++d->transactionsCount_;
    return true;
}

size_t Pool::transactions_count() const noexcept {
    // return d->transactionsCount_; // bad work
    return d->transactions_.size();
}

void Pool::recount() noexcept {
    d->transactionsCount_ = static_cast<uint32_t>(d->transactions_.size());
}

cs::Sequence Pool::sequence() const noexcept {
    return d->sequence_;
}

const cs::PublicKey& Pool::writer_public_key() const noexcept {
    if (d->confidants_.size() == 0) {
        if (d->sequence_ > 0) {
            cserror() << "The pool #" << d->sequence_ << " doesn't contain the confidants";
        }
        return csdb::Pool::priv::zero_writer_public_key_;
    }
    size_t index = 0;
    auto mask = cs::Utils::bitsToMask(d->numberTrusted_, d->realTrusted_);
    for (const auto& it : mask) {
        if (it != 255) {
            break;
        }
        ++index;
    }
    return d->confidants_.at(index);
}

// const cs::Signature& Pool::signature() const noexcept {
//  return d->signature_;
//}

const std::vector<cs::PublicKey>& Pool::confidants() const noexcept {
    return d->confidants_;
}

const std::vector<cs::Signature>& Pool::signatures() const noexcept {
    return d->signatures_;
}

const ::std::vector<csdb::Pool::SmartSignature>& Pool::smartSignatures() const noexcept {
    return d->smartSignatures_;
}

const csdb::Amount& Pool::roundCost() const noexcept {
    return d->roundCost_;
}

void Pool::set_version(uint8_t version) noexcept {
    if (d.constData()->read_only_) {
        return;
    }

    priv* data = d.data();
    data->is_valid_ = true;
    data->version_ = version;
}

void Pool::set_sequence(cs::Sequence seq) noexcept {
    if (d.constData()->read_only_) {
        return;
    }

    priv* data = d.data();
    data->is_valid_ = true;
    data->sequence_ = seq;
}

void Pool::setRoundCost(const csdb::Amount& roundCost) noexcept {
    if (d.constData()->read_only_) {
        return;
    }

    priv* data = d.data();
    data->is_valid_ = true;
    data->roundCost_ = roundCost;
}

void Pool::set_previous_hash(PoolHash previous_hash) noexcept {
    if (d.constData()->read_only_) {
        return;
    }

    priv* data = d.data();
    data->is_valid_ = true;
    data->previous_hash_ = std::move(previous_hash);
}

void Pool::set_confidants(const std::vector<cs::PublicKey>& confidants) noexcept {
    if (d.constData()->read_only_) {
        return;
    }

    priv* data = d.data();
    data->is_valid_ = true;
    data->confidants_ = confidants;
}

void Pool::set_signatures(std::vector<cs::Signature>& blockSignatures) noexcept {
    if (d.constData()->read_only_) {
        csmeta(cswarning) << "Set signatures is failed. Data is read only!";
        return;
    }

    priv* data = d.data();
    data->is_valid_ = true;
    data->signatures_ = std::move(blockSignatures);
}

void Pool::add_smart_signature(const csdb::Pool::SmartSignature& smartSignature) noexcept {
    priv* data = d.data();
    data->is_valid_ = true;
    data->smartSignatures_.emplace_back(smartSignature);
}

void Pool::add_round_confirmations(const std::vector<cs::Signature>& confirmations) noexcept {
    priv* data = d.data();
    data->is_valid_ = true;
    data->roundConfirmations_ = std::move(confirmations);
}

void Pool::add_real_trusted(const uint64_t trustedMask) noexcept {
    priv* data = d.data();
    data->is_valid_ = true;
    data->realTrusted_ = trustedMask;
}

void Pool::add_confirmation_mask(const uint64_t confMask) noexcept {
    priv* data = d.data();
    data->is_valid_ = true;
    data->roundConfirmationMask_ = confMask;
}

void Pool::add_number_trusted(const uint8_t trustedNumber) noexcept {
    priv* data = d.data();
    data->is_valid_ = true;
    data->numberTrusted_ = trustedNumber;
}

void Pool::add_number_confirmations(const uint8_t confNumber) noexcept {
    priv* data = d.data();
    data->is_valid_ = true;
    data->numberConfirmations_ = confNumber;
}

void Pool::set_storage(const Storage& storage) noexcept {
    // We can set up storage even if Pool is read-only
    priv* data = d.data();
    data->is_valid_ = true;
    data->storage_ = storage.weak_ptr();
}

Pool::Transactions& Pool::transactions() {
    return d->transactions_;
}

const Pool::Transactions& Pool::transactions() const {
    return d->transactions_;
}

Pool::NewWallets* Pool::newWallets() noexcept {
    if (d.constData()->read_only_) {
        return nullptr;
    }
    return &d->newWallets_;
}

const Pool::NewWallets& Pool::newWallets() const noexcept {
    return d->newWallets_;
}

bool Pool::add_user_field(user_field_id_t id, const UserField& field) noexcept {
    if (d.constData()->read_only_ || (!field.is_valid())) {
        return false;
    }

    priv* data = d.data();
    data->is_valid_ = true;
    data->user_fields_[id] = field;

    return true;
}

UserField Pool::user_field(user_field_id_t id) const noexcept {
    const priv* data = d.constData();
    auto it = data->user_fields_.find(id);
    return (data->user_fields_.end() == it) ? UserField{} : it->second;
}

::std::set<user_field_id_t> Pool::user_field_ids() const noexcept {
    ::std::set<user_field_id_t> res;
    const priv* data = d.constData();
    for (const auto& it : data->user_fields_) {
        res.insert(it.first);
    }
    return res;
}

bool Pool::compose() {
    if (d.constData()->read_only_) {
        return true;
    }

    d->compose();

    return d.constData()->is_valid_;
}

cs::Bytes Pool::to_binary() const noexcept {
    return d->binary_representation_;
}

uint64_t Pool::get_time() const noexcept {
    return atoll(user_field(0).value<std::string>().c_str());
}

Pool Pool::from_binary(cs::Bytes&& data) {
    std::unique_ptr<priv> p{new priv()};
    ::csdb::priv::ibstream is(data.data(), data.size());
    if (!p->get(is)) {
        return Pool();
    }
    p->update_binary_representation(std::move(data));
    p->update_transactions();
    return Pool(p.release());
}

Pool Pool::meta_from_binary(cs::Bytes&& data, size_t& cnt) {
    std::unique_ptr<priv> p(new priv());
    ::csdb::priv::ibstream is(data.data(), data.size());

    if (!p->get_meta(is, cnt)) {
        return Pool();
    }

    p->update_binary_representation(std::move(data));
    return Pool(p.release());
}

Pool Pool::meta_from_byte_stream(const char* data, size_t size) {
    std::unique_ptr<priv> p(new priv());
    ::csdb::priv::ibstream is(data, size);

    size_t t;
    if (!p->get_meta(is, t)) {
        return Pool();
    }

    return Pool(p.release());
}

Pool Pool::from_lz4_byte_stream(size_t uncompressedSize) {
    std::unique_ptr<priv> p(new priv());
    p->binary_representation_.resize(uncompressedSize);

    ::csdb::priv::ibstream is(p->binary_representation_.data(), p->binary_representation_.size());

    if (!p->get(is)) {
        return Pool();
    }

    p->updateHash();

    return Pool(p.release());
}

char* Pool::to_byte_stream(uint32_t& size) {
    if (d->binary_representation_.empty()) {
        d->update_binary_representation();
    }

    size = static_cast<uint32_t>(d->binary_representation_.size());
    return reinterpret_cast<char*>(d->binary_representation_.data());
}

bool Pool::save(Storage storage) {
    if ((!d.constData()->is_valid_)) {
        return false;
    }

    Storage s = d->get_storage(std::move(storage));
    if (!s.isOpen()) {
        return false;
    }

    if (d->hash_.is_empty()) {
        d->updateHash();
    }

    if (s.pool_save(*this)) {
        d->storage_ = s.weak_ptr();
        return true;
    }

    return false;
}

cs::Bytes Pool::to_byte_stream_for_sig() {
    ::csdb::priv::obstream os;
    d->put(os, true);
    cs::Bytes result = std::move(const_cast<std::vector<uint8_t>&>(os.buffer()));
    return result;
}

Pool Pool::load(const PoolHash& hash, Storage storage) {
    if (!storage.isOpen()) {
        storage = ::csdb::defaultStorage();
    }

    Pool res = storage.pool_load(hash);
    if (res.is_valid()) {
        res.set_storage(storage);
    }
    return res;
}

bool Pool::getWalletAddress(const NewWalletInfo& info, csdb::Address& wallAddress) const {
    const csdb::Pool::Transactions& transactions = this->transactions();
    const auto& conf = this->confidants();
    size_t max_new_wallets_size = transactions.size() + conf.size();

    size_t idx = info.addressId_.trxInd_;
    if (idx >= max_new_wallets_size) {
        return false;
    }
    if (idx < transactions.size()) {
        csdb::Transaction trx = transactions[idx];
        const bool isSource = (info.addressId_.addressType_ == NewWalletInfo::AddressType::AddressIsSource);
        wallAddress = (isSource) ? trx.source() : trx.target();
        return true;
    }

    wallAddress = csdb::Address::from_public_key(this->confidants()[idx - transactions.size()]);
    return true;
}

void Pool::NewWalletInfo::put(::csdb::priv::obstream& os) const {
    union {
        Pool::NewWalletInfo::AddressId address_id;
        size_t asSizeType;
    } addresIdConverter{};

    addresIdConverter.address_id = addressId_;
    os.put(addresIdConverter.asSizeType);
    os.put(walletId_);
}

bool Pool::NewWalletInfo::get(::csdb::priv::ibstream& is) {
    size_t address_id;
    if (!is.get(address_id)) {
        return false;
    }
    auto id = reinterpret_cast<size_t*>(&addressId_);
    *id = address_id;
    return is.get(walletId_);
}

}  // namespace csdb
